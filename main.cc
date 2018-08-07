/* Command line application that encodes a set of WAV files to MP3
 * Oleg Prosekov 2018-08-05
 */
#include <iostream>
#include <list>
#include <string>
#include <vector>
#include <vector>
#include <sstream>
#include <time.h>
#include <stdio.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/sysinfo.h>
#include "lame.h"
#include "wave.h"

static pthread_mutex_t mutex_finished = PTHREAD_MUTEX_INITIALIZER;

using namespace std;

typedef struct {
	vector<string> *file_name;
	bool *flag_enc;
	int num_files;
	int th_id;
	int num_enc;
} ENC_ARGS;

/* Parse a directory <dirname> and return a list of
 * filenames within this directory. */
vector<string> parse_directory(const char *dirname)
{
    struct dirent *de;  // Pointer for directory entry
    vector<string> dircontx;
    DIR *dr = opendir(dirname);
 
    if (dr == NULL) {
        printf("Could not open current directory\n");
	    return dircontx;
    }
 
    // http://pubs.opengroup.org/onlinepubs/7990989775/xsh/readdir.html
    printf("\n--- dir <%s>::\n", dirname);
    while ((de = readdir(dr)) != NULL) {
            int len = strlen(de->d_name);
            //printf("len = %d\n", len);
            if (len > 4)
                if (!strcmp(de->d_name+len-4, ".wav")) {
                    dircontx.push_back(string(dirname) + "/" + string(de->d_name).substr(0,len-4));
                    printf("%s\n", de->d_name);
                }
    }
    printf("--- Found %ld WAV file(s)\n\n", dircontx.size());
 
    closedir(dr);

	return dircontx;
}

void *lame_encoder(void* arg)
{
	int ret;
	ENC_ARGS *args = (ENC_ARGS*)arg;

	while (true) {
		bool wflag = false;
		int file_id = -1;

		pthread_mutex_lock(&mutex_finished);

		for (int i = 0; i < args->num_files; i++) {
			if (!args->flag_enc[i]) {
				args->flag_enc[i] = true;
				file_id = i;
				wflag = true;
				break;
			}
		}

		pthread_mutex_unlock(&mutex_finished);

		if (!wflag)
			return NULL; // break

		string wav = args->file_name->at(file_id)+".wav";
		string mp3 = args->file_name->at(file_id)+".mp3";
        const char* wav_filename = wav.c_str();
		const char* mp3_filename = mp3.c_str();

		FMT_DATA *hdr = NULL;
        short *left = NULL, *right = NULL;
		// init encoding params
		lame_global_flags *gfp = lame_init();
		lame_set_quality(gfp, 5); // good quality level

		// parse wav file
		int data_sz = -1;
		ret = read_wave(wav_filename, hdr, left, right, data_sz);
		if (ret) {
			fprintf(stderr, "Error in file %s. Skipping.\n\n", wav_filename);
			continue;
		}

		lame_set_num_channels(gfp, hdr->wChannels);
		lame_set_num_samples(gfp, data_sz / hdr->wBlockAlign);

		ret = lame_init_params(gfp);
		if (ret != 0) {
			fprintf(stderr, "Invalid encoding parameters! Skipping file.\n\n");
			continue;
		}


        int numSamples = data_sz / hdr->wBlockAlign;
        int mp3BufferSize = numSamples * 5 / 4 + 7200; // worst case estimate
        unsigned char *mp3Buffer = (unsigned char*)malloc(mp3BufferSize);

        // call to lame
        int mp3size = lame_encode_buffer(gfp, (short*)left, (short*)right, numSamples, mp3Buffer, mp3BufferSize);

        if (!(mp3size > 0)) {
            free(mp3Buffer);
            fprintf(stderr, "No data was encoded by lame_encode_buffer. Return code: %d\n", mp3size);
            fprintf(stderr, "Unable to encode mp3: %s\n\n", mp3_filename);
            continue;
        }

        // write to file
        FILE *out = fopen(mp3_filename, "wb+");
        fwrite((void*)mp3Buffer, sizeof(unsigned char), mp3size, out);
        int flushSize = lame_encode_flush(gfp, mp3Buffer, mp3BufferSize);
        fwrite((void*)mp3Buffer, sizeof(unsigned char), flushSize, out);

        // might be omitted
        lame_mp3_tags_fid(gfp, out);

        fclose(out);
        free(mp3Buffer);


		printf("[Thread:%i -- %s]\n", args->th_id, mp3_filename);
		++args->num_enc;

		lame_close(gfp);
		if  (left != NULL) free(left);
		if (right != NULL) free(right);
		if  (hdr  != NULL) free(hdr);
	}

	pthread_exit(NULL);
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
	vector<string> wavfiles = parse_directory(argv[1]);
	int nfiles = wavfiles.size();
    nprocs = min(nprocs, nfiles);

	if (!nfiles) return 0;

	// initialize flag_enc array which contains true for all files which are currently already converted
	bool flag_enc[nfiles];
	for (int i = 0; i < nfiles; i++) flag_enc[i] = false;


	// initialize threads array and argument arrays
	pthread_t *threads = new pthread_t[nprocs];
	ENC_ARGS *threadArgs = (ENC_ARGS*)malloc(nprocs * sizeof(ENC_ARGS));
	for (int i = 0; i < nprocs; i++) {
		threadArgs[i].num_files = nfiles;
		threadArgs[i].file_name = &wavfiles;
		threadArgs[i].flag_enc = flag_enc;
		threadArgs[i].th_id = i;
		threadArgs[i].num_enc = 0;
	}

	clock_t start_clk = clock();

	for (int i = 0; i < nprocs; i++) {
		pthread_create(&threads[i], NULL, lame_encoder, (void*)&threadArgs[i]);
	}

	for (int i = 0; i < nprocs; i++) {
		int ret = pthread_join(threads[i], NULL);
		if (ret != 0) printf("\tpthread error occured!!!\n");
	}
    printf("\n");

	clock_t end_clk = clock();

	int iProcessedTotal = 0;
	for (int i = 0; i < nprocs; i++) {
		printf("Thread %d processed %d files.\n", i, threadArgs[i].num_enc);
		iProcessedTotal += threadArgs[i].num_enc;
	}

	printf("\nEncoded %d out of %d files in total in %g sec.\n", iProcessedTotal, nfiles, double(end_clk - start_clk) / CLOCKS_PER_SEC);

	free(threads);
	free(threadArgs);

	if (iProcessedTotal > nfiles) return 1;

	return 0;
}