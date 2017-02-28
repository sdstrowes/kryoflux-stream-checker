#include <math.h>
#include <png.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "stream.h"
#include "vis.h"

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

void plot_track(struct track *track, struct colour *buffer, uint16_t width, uint16_t height)
{
	uint32_t pos = 0;

	uint32_t start = track->indices[0].stream_pos;
	uint32_t end   = track->indices[1].stream_pos;

	// Seek forward to start
	uint32_t j = 0;
	while (j < track->flux_array_idx && pos < start) {
		pos++;
	}


	// parse whole track
	double fraction = (360.0/(end-start));
	while (pos < track->flux_array_idx && pos < end) {
		float x = (track->track * 0.5 + 42) * sin((pos-start)*fraction*(M_PI/180));
		float y = (track->track * 0.5 + 42) * cos((pos-start)*fraction*(M_PI/180));
		uint32_t x_scaled = x*20 + (width/2);
		uint32_t y_scaled = y*20 + (height/2);

//		printf("pos: %u start:%u end:%u angle:%f x:%f y:%f, x:%u y:%u, fx:%u\n",
//			pos-start, start, end, (pos-start)*fraction,
//			x, y, x_scaled, y_scaled, track->flux_array[pos]);

		if (track->flux_array[pos]/track->sample_clock > 0.0000075) {
			buffer[y_scaled*width + x_scaled].r = 255;
		}
		if (track->flux_array[pos]/track->sample_clock > 0.0000055) {
			buffer[y_scaled*width + x_scaled].g = 255;
		}
		if (track->flux_array[pos]/track->sample_clock > 0.0000035) {
			buffer[y_scaled*width + x_scaled].b = 255;
		}
		

		pos++;
	}

}

