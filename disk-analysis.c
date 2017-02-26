#include <math.h>
#include <png.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "stream.h"


#define TRACK_MAX 84
#define SIDES      2


void print_help(char *binary_name)
{
	printf("usage: %s -n basename\n", binary_name);
}


void setRGB(png_byte *ptr, struct colour val)
{
	ptr[0] = val.r;
	ptr[1] = val.g;
	ptr[2] = val.b;
}

int dump_stream_img(struct colour *buffer, uint16_t width, uint16_t height)
{
	int code = 0;
	FILE *fp = NULL;
	png_structp png_ptr = NULL;
	png_infop info_ptr = NULL;
	png_bytep row = NULL;
	char *filename = "/tmp/foo.png";
	char *title = "test";

	// Open file for writing (binary mode)
	fp = fopen(filename, "wb");
	if (fp == NULL) {
		fprintf(stderr, "Could not open file %s for writing\n", filename);
		code = 1;
		goto finalise;
	}

	// Initialize write structure
	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (png_ptr == NULL) {
		fprintf(stderr, "Could not allocate write struct\n");
		code = 1;
		goto finalise;
	}

	// Initialize info structure
	info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL) {
		fprintf(stderr, "Could not allocate info struct\n");
		code = 1;
		goto finalise;
	}

	// Setup Exception handling
	if (setjmp(png_jmpbuf(png_ptr))) {
		fprintf(stderr, "Error during png creation\n");
		code = 1;
		goto finalise;
	}

	png_init_io(png_ptr, fp);

	// Write header (8 bit colour depth)
	png_set_IHDR(png_ptr, info_ptr, width, height,
			8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
			PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

	// Set title
	if (title != NULL) {
		png_text title_text;
		title_text.compression = PNG_TEXT_COMPRESSION_NONE;
		title_text.key = "Title";
		title_text.text = title;
		png_set_text(png_ptr, info_ptr, &title_text, 1);
	}

	png_write_info(png_ptr, info_ptr);

	// Allocate memory for one row (3 bytes per pixel - RGB)
	row = (png_bytep) malloc(3 * width * sizeof(png_byte));
	memset(row, 0, 3 * width * sizeof(png_byte));

	// Write image data
	int x, y;
	for (y=0 ; y<height ; y++) {
		for (x=0 ; x<width ; x++) {
			setRGB(&(row[x*3]), buffer[y*width + x]);
		}
		png_write_row(png_ptr, row);
	}

	// End write
	png_write_end(png_ptr, NULL);

	free(row); row = NULL;

finalise:
	if (fp != NULL) fclose(fp);
	if (info_ptr != NULL) {
		png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
		png_destroy_info_struct(png_ptr, &info_ptr);
	}
	if (png_ptr != NULL) png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
	if (row != NULL) free(row);

	return code;
}

int main(int argc, char *argv[])
{
	char c;
	char *fn_prefix = NULL;
	struct colour *img_buffer;
	uint16_t width  = 4200;
	uint16_t height = 4200;

	img_buffer = (struct colour *) malloc(width * height * sizeof(struct colour));
	if (img_buffer == NULL) {
		fprintf(stderr, "Could not create image buffer\n");
		return -1;
	}
	memset(img_buffer, 0, width * height * sizeof(struct colour));

	struct track *track;
	track = (struct track *)malloc(sizeof(struct track)*TRACK_MAX*SIDES);
	if (track == NULL) {
		fprintf(stderr, "Could not create track buffer\n");
		return -1;
	}
	memset(track, 0, sizeof(struct track)*TRACK_MAX*SIDES);

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
			printf("--> %s\n", fn);
			parse_stream(fn, &track[side ? track_num + TRACK_MAX : track_num], side, track_num);
			free(fn);
		}
	}

//	for (track_num = 0; track_num < TRACK_MAX; track_num++) {
//		for (side = 0; side < SIDES; side++) {
//			decode_stream(&track[side ? track_num + TRACK_MAX : track_num]);
//		}
//	}


//	for (track = 0; track < TRACK_MAX; track++) {
//		for (side = 0; side < SIDES; side++) {
//			dump_stream(&track[(side*track)+track]);
//		}
//	}

	for (track_num = 0; track_num < 82; track_num++) {
		//for (side = 0; side < SIDES; side++) {
			plot_track(&track[side ? track_num + TRACK_MAX : track_num], img_buffer, width, height);
		//}
	}

	dump_stream_img(img_buffer, width, height);

	free(img_buffer); img_buffer = NULL;
	for (track_num = 0; track_num < TRACK_MAX; track_num++) {
		for (side = 0; side < SIDES; side++) {
			free_stream(&track[side ? track_num + TRACK_MAX : track_num]);
		}
	}
	free(track);

	return 0;
}

