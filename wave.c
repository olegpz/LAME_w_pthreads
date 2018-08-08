#include "wave.h"

// function implementations
int read_wave_header(FILE *file, FMT_DATA *&wav_hdr, int &wav_data_sz, int &wav_offset)
{
	if (!file) return EXIT_FAILURE; // check if file is open
	fseek(file, 0, SEEK_SET); // rewind file

	ANY_CHUNK_HDR chunk_hdr;

	// read and validate RIFF header first
	RIFF_HDR riff_hdr;
	fread(&riff_hdr, sizeof(RIFF_HDR), 1, file);
	if (EXIT_SUCCESS != check_riff_header(&riff_hdr))
		return EXIT_FAILURE;

	// then continue parsing file until we find the 'fmt ' chunk
	bool fmt = false;
	while (!fmt && !feof(file)) {
		fread(&chunk_hdr, sizeof(ANY_CHUNK_HDR), 1, file);
		if (0 == strncmp(chunk_hdr.ID, "fmt ", 4)) {
			// rewind and parse the complete chunk
			fseek(file, (int)ftell(file)-sizeof(ANY_CHUNK_HDR), SEEK_SET);
			
			wav_hdr = new FMT_DATA;
			fread(wav_hdr, sizeof(FMT_DATA), 1, file);
			fmt = true;
			break;
		} else {
			// skip this chunk (i.e. the next chunk_size bytes)
			fseek(file, chunk_hdr.chunk_size, SEEK_CUR);
		}
	}
	if (!fmt) { // found 'fmt ' at all?
		fprintf(stderr, "FATAL: Found no 'fmt ' chunk in file.\n");
	} else if (EXIT_SUCCESS != check_format_data(wav_hdr)) { // if so, check settings
		delete wav_hdr;
		return EXIT_FAILURE;
	}

	// finally, look for 'data' chunk
	bool data = false;
	while (!data && !feof(file)) {
		fread(&chunk_hdr, sizeof(ANY_CHUNK_HDR), 1, file);
		if (0 == strncmp(chunk_hdr.ID, "data", 4)) {
			data = true;
			wav_data_sz = chunk_hdr.chunk_size;
			wav_offset = ftell(file);
			break;
		} else { // skip chunk
			fseek(file, chunk_hdr.chunk_size, SEEK_CUR);
		}
	}
	if (!data) { // found 'data' at all?
		fprintf(stderr, "FATAL: Found no 'data' chunk in file.\n");
		delete wav_hdr;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

int check_format_data(const FMT_DATA *wav_hdr)
{
	if (wav_hdr->tag != 0x01) {
		fprintf(stderr, "Bad non-PCM format: %x\n", wav_hdr->tag);
		return EXIT_FAILURE;
	}
	if (wav_hdr->num_channels != 1 && wav_hdr->num_channels != 2) {
		fprintf(stderr, "Bad number of channels (only mono or stereo supported).\n");
		return EXIT_FAILURE;
	}
	if (wav_hdr->chunk_size != 16) {
		fprintf(stderr, "WARNING: 'fmt ' chunk size seems to be off.");
	}
	if (wav_hdr->block_align != wav_hdr->bits * wav_hdr->num_channels / 8) {
		fprintf(stderr, "WARNING: 'fmt ' has strange bytes/bits/channels configuration.");
	}
	
	return EXIT_SUCCESS;
}

int check_riff_header(const RIFF_HDR *riff_hdr)
{
	if (0 == strncmp(riff_hdr->riffID, "RIFF", 4) && 0 == strncmp(riff_hdr->wavID, "WAVE", 4) && riff_hdr->file_len > 0)
		return EXIT_SUCCESS;

	fprintf(stderr, "Bad RIFF header!");
	return EXIT_FAILURE;
}

void get_pcm_channels_from_wave(FILE *file, const FMT_DATA* wav_hdr, short* &wav_left, short* &wav_right, const int wav_data_sz,
	const int wav_offset)
{
	int idx;
	int numSamples = wav_data_sz / wav_hdr->block_align;

	wav_left  = NULL;
	wav_right = NULL;

	// allocate PCM arrays
	wav_left = new short[wav_data_sz / wav_hdr->num_channels / sizeof(short)];
	if (wav_hdr->num_channels > 1)
		wav_right = new short[wav_data_sz / wav_hdr->num_channels / sizeof(short)];

	// capture each sample
	fseek(file, wav_offset, SEEK_SET);// set file pointer to beginning of data array

	if (wav_hdr->num_channels == 1) {
		fread((void*)wav_left, wav_hdr->block_align, numSamples, file);
	} else {
		for (idx = 0; idx < numSamples; idx++) {
			fread((void*)&wav_left[idx], wav_hdr->block_align / wav_hdr->num_channels, 1, file);
			if (wav_hdr->num_channels>1)
				fread((void*)&wav_right[idx], wav_hdr->block_align / wav_hdr->num_channels, 1, file);
		}
	}

	assert(wav_right == NULL || wav_hdr->num_channels != 1);

}

int read_wave(const char *filename, FMT_DATA* &wav_hdr, short* &wav_left, short* &wav_right, int &wav_data_sz)
{

	FILE *wav_file = fopen(filename, "rb");
	if (wav_file) {
		// parse file
		int wav_offset = 0;
		if (EXIT_SUCCESS != read_wave_header(wav_file, wav_hdr, wav_data_sz, wav_offset)) {
			return EXIT_FAILURE;
		}
		get_pcm_channels_from_wave(wav_file, wav_hdr, wav_left, wav_right, wav_data_sz, wav_offset);
		fclose(wav_file);

		// cleanup and return
		return EXIT_SUCCESS;
	}
	return EXIT_FAILURE;
}
