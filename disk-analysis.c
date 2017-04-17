#include <getopt.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "vis.h"
#include "stream.h"
#include "disk-analysis-log.h"

#define TRACK_MAX 84
#define SIDES      2


void print_help(char *binary_name)
{
	printf("usage: %s -n basename\n", binary_name);
}


int main(int argc, char *argv[])
{
	char c;
	char *fn_prefix = NULL;
	struct colour *img_buffer;
	uint16_t width  = 4200;
	uint16_t height = 4200;

	log_init("", LOG_INFO);

	img_buffer = (struct colour *) malloc(width * height * sizeof(struct colour));
	if (img_buffer == NULL) {
		log_err("Could not create image buffer");
		return -1;
	}
	memset(img_buffer, 0, width * height * sizeof(struct colour));

	struct track *track;
	track = (struct track *)malloc(sizeof(struct track)*TRACK_MAX*SIDES);
	if (track == NULL) {
		log_err("Could not create track buffer");
		return -1;
	}
	memset(track, 0, sizeof(struct track)*TRACK_MAX*SIDES);

	opterr = 0;	// silence error output on bad options
	while ((c = getopt (argc, argv, "n:")) != -1) {
		switch (c) {
		case 'n': {
			fn_prefix = optarg;
			break;
		}
		}
	}

	if (fn_prefix == NULL) {
		print_help(argv[0]);
		exit(1);
	}

	int track_num;
	int side;
	char *fn;
	for (track_num = 0; track_num < TRACK_MAX; track_num++) {
		for (side = 0; side < SIDES; side++) {
			fn = (char *)malloc(strlen(fn_prefix) + 8 + 1);
			sprintf(fn, "%s%02u.%u.raw", fn_prefix, track_num, side);
			log_msg("--> %s", fn);
			parse_stream(fn, &track[side ? track_num + TRACK_MAX : track_num], side, track_num);
			free(fn);
		}
	}

	for (track_num = 0; track_num < TRACK_MAX; track_num++) {
		for (side = 0; side < SIDES; side++) {
			decode_stream(&track[side ? track_num + TRACK_MAX : track_num]);
		}
	}


//	for (track = 0; track < TRACK_MAX; track++) {
//		for (side = 0; side < SIDES; side++) {
//			dump_stream(&track[(side*track)+track]);
//		}
//	}

//	for (track_num = 0; track_num < 82; track_num++) {
//		//for (side = 0; side < SIDES; side++) {
//			plot_track(&track[side ? track_num + TRACK_MAX : track_num], img_buffer, width, height);
//		//}
//	}
//
//	dump_stream_img(img_buffer, width, height);

	free(img_buffer); img_buffer = NULL;
	for (track_num = 0; track_num < TRACK_MAX; track_num++) {
		for (side = 0; side < SIDES; side++) {
			free_stream(&track[side ? track_num + TRACK_MAX : track_num]);
		}
	}
	free(track);

	return 0;
}

