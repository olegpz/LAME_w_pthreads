/* Command line application that encodes a set of WAV files to MP3
 * Oleg Prosekov 2018-08-05
 */
#include <iostream>
#include <list>
#include <string>
#include <vector>
#include <ctime>
#include "dirent.h"	/* this is used to get cross-platform directory listings without Boost. */

//#include "lame_interface.h"
#include <vector>
#include <sstream>
#include "lame.h"
#include "wave.h"
#include "pthread.h"
#ifndef WIN32
#include "sys/sysinfo.h"
#endif

static pthread_mutex_t mutFilesFinished = PTHREAD_MUTEX_INITIALIZER;

#ifdef WIN32
#define PATHSEP "\\"
#else
#define PATHSEP "/"
#endif

using namespace std;

/*
 * POSIX-conforming argument struct for worker routine 'complete_encode_worker'.
 */
typedef struct {
	vector<string> *pFilenames;
	bool *pbFilesFinished;
	int iNfiles;
	int iThreadId;
	int iProcessedFiles;
} ENC_WRK_ARGS;

/* Used for filtering filename extensions. Returns true if 'fullString' ends with
 * 'subString', otherwise false.
 */
bool string_ends_with(const string &fullString, const string &subString)
{
	if (subString.length() > fullString.length()) return false;
	else {
		// lowercase conversion
		string fullString_l = fullString;
		for (unsigned int i = 0; i < fullString_l.length(); i++) {
			if ('A' <= fullString_l[i] && fullString_l[i] <= 'Z')
				fullString_l[i] = fullString_l[i] - ('Z' - 'z');
		}
		string subString_l = subString;
		for (unsigned int i = 0; i < subString_l.length(); i++) {
			if ('A' <= subString_l[i] && subString_l[i] <= 'Z')
				subString_l[i] = subString_l[i] - ('Z' - 'z');
		}
		return (fullString_l.compare(fullString_l.length() - subString_l.length(), subString_l.length(),
			subString_l) == 0);
	}
}

/* Parse a directory given by dirname and return a list of
 * filenames and subfolders within this directory. 
 */
list<string> parse_directory(const char *dirname)
{
	DIR *dir;
	dirent *ent;
	list<string> dirEntries;

	if ((dir = opendir(dirname)) != NULL) {
		// list directory
		while ((ent = readdir(dir)) != NULL) {
			dirEntries.push_back(string(ent->d_name));
		}
		closedir(dir);
	} else {
		cerr << "FATAL: Unable to parse directory." << endl;
		exit(1);
	}

	return dirEntries;
}

/* encode_to_file
 *  Main encoding routine which reads input information from gfp and hdr as well as one or two PCM buffers,
 *  encodes it to MP3 and directly stores the MP3 data in the file given by filename.
 *  Calls lame_encode_buffer, lame_encode_flush, and lame_mp3_tags_fid internally for a complete conversion
 *  process.
 */
int encode_to_file(lame_global_flags *gfp, const FMT_DATA *hdr, const short *leftPcm, const short *rightPcm,
	const int iDataSize, const char *filename)
{
	int numSamples = iDataSize / hdr->wBlockAlign;

	int mp3BufferSize = numSamples * 5 / 4 + 7200; // worst case estimate
	unsigned char *mp3Buffer = new unsigned char[mp3BufferSize];

	// call to lame_encode_buffer
	int mp3size = lame_encode_buffer(gfp, (short*)leftPcm, (short*)rightPcm, numSamples, mp3Buffer, mp3BufferSize);
	if (!(mp3size > 0)) {
		delete[] mp3Buffer;
		cerr << "No data was encoded by lame_encode_buffer. Return code: " << mp3size << endl;
		return 1;
	}

	// write to file
	FILE *out = fopen(filename, "wb+");
	fwrite((void*)mp3Buffer, sizeof(unsigned char), mp3size, out);

	// call to lame_encode_flush
	int flushSize = lame_encode_flush(gfp, mp3Buffer, mp3BufferSize);

	// write flushed buffers to file
	fwrite((void*)mp3Buffer, sizeof(unsigned char), flushSize, out);

	// call to lame_mp3_tags_fid (might be omitted)
	lame_mp3_tags_fid(gfp, out);

	fclose(out);
	delete[] mp3Buffer;

#ifdef __VERBOSE_
	cout << "Wrote " << mp3size + flushSize << " bytes." << endl;
#endif
	return 0;
}

/* complete_encode_worker
 *  Main worker thread routine which is supplied with a list of filenames, a status array indicating which files
 *  are already worked upon, and some additional info via a ENC_WRK_ARGS struct.
 *  As long as there are still unprocessed filenames, this routine will fetch the next free filename, mark it as
 *  processed, and execute the complete conversion from reading .wav to writing .mp3.
 */
void *complete_encode_worker(void* arg)
{
	int ret;
	ENC_WRK_ARGS *args = (ENC_WRK_ARGS*)arg; // parse argument struct

	while (true) {
#ifdef __VERBOSE_
		cout << "Checking for work\n";
#endif
		// determine which file to process next
		bool bFoundWork = false;
		int iFileIdx = -1;

		pthread_mutex_lock(&mutFilesFinished);
		for (int i = 0; i < args->iNfiles; i++) {
			if (!args->pbFilesFinished[i]) {
				args->pbFilesFinished[i] = true; // mark as being worked on
				iFileIdx = i;
				bFoundWork = true;
				break;
			}
		}
		pthread_mutex_unlock(&mutFilesFinished);

		if (!bFoundWork) {// done yet?
			return NULL; // break
		}
		string sMyFile = args->pFilenames->at(iFileIdx);
		string sMyFileOut = sMyFile.substr(0, sMyFile.length() - 3) + "mp3";

		// start working
		FMT_DATA *hdr = NULL;
		short *leftPcm = NULL, *rightPcm = NULL;
		// init encoding params
		lame_global_flags *gfp = lame_init();
		lame_set_brate(gfp, 192); // increase bitrate
		lame_set_quality(gfp, 3); // increase quality level
		lame_set_bWriteVbrTag(gfp, 0);

		// parse wave file
#ifdef __VERBOSE_
		printf("Parsing %s ...\n", sMyFile.c_str());
#endif
		int iDataSize = -1;
		ret = read_wave(sMyFile.c_str(), hdr, leftPcm, rightPcm, iDataSize);
		if (ret != 0) {
			printf("Error in file %s. Skipping.\n", sMyFile.c_str());
			continue; // see if there's more to do
		}

		lame_set_num_channels(gfp, hdr->wChannels);
		lame_set_num_samples(gfp, iDataSize / hdr->wBlockAlign);
		// check params
		ret = lame_init_params(gfp);
		if (ret != 0) {
			cerr << "Invalid encoding parameters! Skipping file." << endl;
			continue;
		}

		// encode to mp3
		ret = encode_to_file(gfp, hdr, leftPcm, rightPcm, iDataSize, sMyFileOut.c_str());
		if (ret != 0) {
			cerr << "Unable to encode mp3: " << sMyFileOut.c_str() << endl;
			continue;
		}

		printf("[:%i][ok] .... %s\n", args->iThreadId, sMyFile.c_str());
		++args->iProcessedFiles;

		lame_close(gfp);
		if (leftPcm != NULL) delete[] leftPcm;
		if (rightPcm != NULL) delete[] rightPcm;
		if (hdr != NULL) delete hdr;
	}

	pthread_exit((void*)0);
}


int main(int argc, char **argv)
{
     // use all available CPU cores for the encoding process in an efficient way by utilizing multi-threading
#ifndef WIN32
	int nprocs = get_nprocs();
#else
	int nprocs = 2;
#endif
	if (argc < 2) {
		printf("Usage: %s <PATH_NAME>\n", argv[0]);
		printf("\tall WAV-files contained in the <PATH_NAME> are to be encoded to MP3\n");
		return 1;
	}
	printf("LAME version: %s\n", get_lame_version());

	// parse directory
	list<string> files = parse_directory(argv[1]);
	vector<string> wavFiles;
	for (list<string>::iterator it = files.begin(); it != files.end(); it++) {
		// check if it's a wave file and add path
		if (string_ends_with(*it, string(".wav"))) {
			wavFiles.push_back(string(argv[1]) + string(PATHSEP) + *it);
		}
	}
	int nfiles = wavFiles.size();
    nprocs = min(nprocs, nfiles);

	printf("Found %d .wav file(s) in directory.", nfiles);
	if (!nfiles) return 0;

	// initialize pbFilesFinished array which contains true for all files which are currently already converted
	bool *pbFilesFinished = new bool[nfiles];
	for (int i = 0; i < nfiles; i++) pbFilesFinished[i] = false;


	// initialize threads array and argument arrays
	pthread_t *threads = new pthread_t[nprocs];
	ENC_WRK_ARGS *threadArgs = (ENC_WRK_ARGS*)malloc(nprocs * sizeof(ENC_WRK_ARGS));
	for (int i = 0; i < nprocs; i++) {
		threadArgs[i].iNfiles = nfiles;
		threadArgs[i].pFilenames = &wavFiles;
		threadArgs[i].pbFilesFinished = pbFilesFinished;
		threadArgs[i].iThreadId = i;
		threadArgs[i].iProcessedFiles = 0;
	}

	clock_t start_clk = clock();

	// create worker threads
	for (int i = 0; i < nprocs; i++) {
		pthread_create(&threads[i], NULL, complete_encode_worker, (void*)&threadArgs[i]);
	}

	// synchronize / join threads
	for (int i = 0; i < nprocs; i++) {
		int ret = pthread_join(threads[i], NULL);
		if (ret != 0) {
			printf("\tpthread error occured!!!\n");
		}
	}

	clock_t end_clk = clock();

	int iProcessedTotal = 0;
	for (int i = 0; i < nprocs; i++) {
		printf("Thread %d processed %d files.\n", i, threadArgs[i].iProcessedFiles);
		iProcessedTotal += threadArgs[i].iProcessedFiles;
	}

	printf("Converted %d out of %d files in total in %g sec.\n", iProcessedTotal, nfiles, double(end_clk - start_clk) / CLOCKS_PER_SEC);

	free(threads);
	free(threadArgs);

	if (iProcessedTotal > nfiles) return 1;

	return 0;
}