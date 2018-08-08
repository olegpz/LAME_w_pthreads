#ifndef __WAVE_H_
#define __WAVE_H_

#include <fstream>
#include <iostream>
#include <cassert>
#include <cstring>
#include <cstdlib>

using namespace std;

/* Initial header of WAV file */
typedef struct {
	         char riffID[4]; // "RIFF"
	unsigned int  file_len;   // length of file minus 8 for "RIFF" and file_len.
	         char wavID[4];  // "WAVE"
} RIFF_HDR;

/* Chunk header and data for 'fmt ' format chunk in WAV file (required).
 * This information determines how the PCM input streams must be interpreted.
 */
typedef struct {
			 char  ID[4]; 			// "fmt "
	unsigned int   chunk_size;      // should be 16
	unsigned short tag;         	// 0x01 for PCM - other modes unsupported
	unsigned short num_channels;    // number of channels (1 mono, 2 stereo)
	unsigned int   samplfreq; 		// e.g. 44100
	unsigned int   byterate;   		// e.g. 4*44100
	unsigned short block_align;     // bytes per sample (all channels, e.g. 4)
	unsigned short bits;  			// bits per sample and channel, e.g. 16
} FMT_DATA;

/* Chunk header for any chunk type in IFF format
 * such as 'fmt ' or 'data' (we ignore everything else).
 */
typedef struct {
			 char ID[4]; // any identifier
	unsigned int  chunk_size;
} ANY_CHUNK_HDR;


/* read_wave
 *  Read a WAV file given by filename, parsing the 44 byte header into wav_hdr (will be allocated),
 *  allocating arrays for left and right (only left for mono input) PCM channels and returning
 *  pointers to them.
 *  This is the 'does-it-all' routine which you should call from outside, it will subsequently
 *  call read_wave_header, check_wave_header, and get_pcm_channels_from_wave itself.
 */
int read_wave(
	const char*			filename,			/* file to open */
	FMT_DATA*			&wav_hdr,			/* stores header info here (will be allocated) */
	short*				&wav_left,			/* stores left (or mono) PCM channel here */
	short*				&wav_right,			/* stores right PCM channel (stereo only) here */
	int					&wav_data_sz		/* size of data array */
);

/* read_wave_header
 *  Parses the given file stream for the WAV header and 'fmt ' as well as 'data' information.
 *
 *  Return value:
 *    EXIT_SUCCESS or EXIT_FAILURE
 */
int read_wave_header(
	FILE*				file,				/* file stream to parse from (must be opened for reading) */
	FMT_DATA*			&wav_hdr,			/* stores header info here (will be allocated) */
	int					&wav_data_sz,		/* stores size of data array here */
	int					&wav_offset			/* stores data offset (first data byte in input file) here */
);

/* check_riff_header
 * Checks validity of initial WAV header ('RIFF', file_len, 'WAVE')
 */
int check_riff_header(const RIFF_HDR *rHdr);

/* check_format_data
 * Checks validity of WAV settings provided by wav_hdr.
 */
int check_format_data(const FMT_DATA *wav_hdr);

/* get_pcm_channels_from_wave
*  Allocates buffers for left and (if stereo) right PCM channels and parses data from filestream.
*  Header wav_hdr must have been read before.
*
*  Return value:
*    EXIT_SUCCESS  if header is valid
*    EXIT_FAILURE  if we probably can't handle that file
*/
void get_pcm_channels_from_wave(
	FILE*				file,				/* file stream to parse from (must be opened for reading) */
	const FMT_DATA*		wav_hdr,			/* pointer to format struct that's already been read */
	short*				&wav_left,			/* stores left PCM channel here (will be allocated) */
	short*				&wav_right,			/* stores right PCM channel here (will be allocated for stereo files)*/
	const int			wav_data_sz,		/* size of PCM data array */
	const int			wav_offset			/* first PCM array byte in file*/
);

#endif //__WAVE_H_
