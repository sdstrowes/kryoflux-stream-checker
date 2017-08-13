#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disk-analysis-log.h"
#include "stream.h"
#include "vis.h"

#ifndef M_PI
#define M_PI           3.14159265358979323846  /* pi */
#endif

//void setRGB(png_byte *ptr, struct colour val)
//{
//	ptr[0] = val.r;
//	ptr[1] = val.g;
//	ptr[2] = val.b;
//}


double radius(int track)
{
	//double radius_min = 100.0;
	//double radius_max = 500.0;
	int num_tracks =  84;

	int canvas_width = 1200;
	int canvas_ctr   = canvas_width / 2;

	double radius_min = canvas_ctr / 4;
	double radius_max = canvas_ctr;

	printf("Track %u; radius_min: %f, radius_max: %f\n", track, radius_min, radius_max);

	return (num_tracks - track) * ((radius_max - radius_min) / num_tracks) + radius_min;
}



double top(int track)
{
	double top = 400;
	double radius_min = 100.0;
	double radius_max = 500.0;
	int num_tracks =  84;

	return top - (track * ((radius_max - radius_min)/num_tracks)) + radius_min;
}

FILE * test_svg_out()
{
	FILE *svg_out = fopen("test.svg", "w");
	if (svg_out == NULL) {
		log_err("Could not open file: %s", strerror(errno));
		return NULL;
	}

	int canvas_width = 1250;

	int img_width = 1200;
	int offset = (canvas_width - img_width)/2;
	int img_ctr   = (img_width / 2) + offset;

	fprintf(svg_out, "<?xml version=\"1.0\" standalone=\"no\"?>\n");
	fprintf(svg_out, "<svg width=\"%u\" height=\"%u\" xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\">\n",
		canvas_width, canvas_width);

	int i = 0;
	for (i = 0; i < 84; i++) {
		double r = radius(i);
		double start_x = img_ctr + offset;
		double start_y = img_ctr - r;
		printf("Track %u; radius: %f\n", i, r);
		fprintf(svg_out, "<path d=\"M %f %f A %f %f, 0, 1, 1, %f %f\" fill=\"none\" stroke=\"blue\" stroke-width=\"2\" />\n",
		start_x+5, start_y, r, r, start_x-5, start_y);
	}

	return svg_out;
}

void finalise_svg(FILE *svg_out)
{
	fprintf(svg_out, "</svg>\n");

	fclose(svg_out);
}

void plot_track(FILE *svg_out, struct track *track)
{
	uint32_t pos = 0;

	if (track == NULL) {
		return;
	}
	printf("Got track %u\n", track->track);

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

		double r = radius(track->track);

		int img_width = 1200;
		int canvas_width = 1250;
		int offset    = (canvas_width - img_width)/2;
		int img_ctr   = (img_width / 2) + offset;

		double x = r * cos((pos-start) * fraction) + img_ctr + offset;
		double y = r * sin((pos-start) * fraction) + img_ctr;


//		printf("pos: %u start:%u end:%u angle:%f x:%f y:%f, x:%u y:%u, fx:%u\n",
//			pos-start, start, end, (pos-start)*fraction,
//			x, y, x_scaled, y_scaled, track->flux_array[pos]);

		printf("SAMPLE %u %f\n", track->track, track->flux_array[pos]/track->sample_clock);

		double sample = track->flux_array[pos]/track->sample_clock;

		if (	!(	(sample > 0.0000075 && sample < 0.0000085) ||
				(sample > 0.0000055 && sample < 0.0000065) ||
				(sample > 0.0000035 && sample < 0.0000045)	)
		) {
			printf("Err location: %f,%f\n", x, y);
			fprintf(svg_out, "<circle cx=\"%f\" cy=\"%f\" r=\"2\" fill=\"red\"/>\n", x, y);
			
		}

		pos++;
	}

}


//int dump_stream_img(struct colour *buffer, uint16_t width, uint16_t height)
//{
//	int code = 0;
//	FILE *fp = NULL;
//	png_structp png_ptr = NULL;
//	png_infop info_ptr = NULL;
//	png_bytep row = NULL;
//	char *filename = "/tmp/foo.png";
//	char *title = "test";
//
//	// Open file for writing (binary mode)
//	fp = fopen(filename, "wb");
//	if (fp == NULL) {
//		log_err("Could not open file %s for writing\n", filename);
//		code = 1;
//		goto finalise;
//	}
//
//	// Initialize write structure
//	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
//	if (png_ptr == NULL) {
//		log_err("Could not allocate write struct\n");
//		code = 1;
//		goto finalise;
//	}
//
//	// Initialize info structure
//	info_ptr = png_create_info_struct(png_ptr);
//	if (info_ptr == NULL) {
//		log_err("Could not allocate info struct\n");
//		code = 1;
//		goto finalise;
//	}
//
//	// Setup Exception handling
//	if (setjmp(png_jmpbuf(png_ptr))) {
//		log_err("Error during png creation\n");
//		code = 1;
//		goto finalise;
//	}
//
//	png_init_io(png_ptr, fp);
//
//	// Write header (8 bit colour depth)
//	png_set_IHDR(png_ptr, info_ptr, width, height,
//			8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
//			PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
//
//	// Set title
//	if (title != NULL) {
//		png_text title_text;
//		title_text.compression = PNG_TEXT_COMPRESSION_NONE;
//		title_text.key = "Title";
//		title_text.text = title;
//		png_set_text(png_ptr, info_ptr, &title_text, 1);
//	}
//
//	png_write_info(png_ptr, info_ptr);
//
//	// Allocate memory for one row (3 bytes per pixel - RGB)
//	row = (png_bytep) malloc(3 * width * sizeof(png_byte));
//	memset(row, 0, 3 * width * sizeof(png_byte));
//
//	// Write image data
//	int x, y;
//	for (y=0 ; y<height ; y++) {
//		for (x=0 ; x<width ; x++) {
//			setRGB(&(row[x*3]), buffer[y*width + x]);
//		}
//		png_write_row(png_ptr, row);
//	}
//
//	// End write
//	png_write_end(png_ptr, NULL);
//
//	free(row); row = NULL;
//
//finalise:
//	if (fp != NULL) fclose(fp);
//	if (info_ptr != NULL) {
//		png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
//		png_destroy_info_struct(png_ptr, &info_ptr);
//	}
//	if (png_ptr != NULL) png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
//	if (row != NULL) free(row);
//
//	return code;
//}
//
//void plot_track(struct track *track, struct colour *buffer, uint16_t width, uint16_t height)
//{
//	uint32_t pos = 0;
//
//	uint32_t start = track->indices[0].stream_pos;
//	uint32_t end   = track->indices[1].stream_pos;
//
//	// Seek forward to start
//	uint32_t j = 0;
//	while (j < track->flux_array_idx && pos < start) {
//		pos++;
//	}
//
//
//	// parse whole track
//	double fraction = (360.0/(end-start));
//	while (pos < track->flux_array_idx && pos < end) {
//		float x = (track->track * 0.5 + 42) * sin((pos-start)*fraction*(M_PI/180));
//		float y = (track->track * 0.5 + 42) * cos((pos-start)*fraction*(M_PI/180));
//		uint32_t x_scaled = x*20 + (width/2);
//		uint32_t y_scaled = y*20 + (height/2);
//
////		printf("pos: %u start:%u end:%u angle:%f x:%f y:%f, x:%u y:%u, fx:%u\n",
////			pos-start, start, end, (pos-start)*fraction,
////			x, y, x_scaled, y_scaled, track->flux_array[pos]);
//
//		if (track->flux_array[pos]/track->sample_clock > 0.0000075) {
//			buffer[y_scaled*width + x_scaled].r = 255;
//		}
//		if (track->flux_array[pos]/track->sample_clock > 0.0000055) {
//			buffer[y_scaled*width + x_scaled].g = 255;
//		}
//		if (track->flux_array[pos]/track->sample_clock > 0.0000035) {
//			buffer[y_scaled*width + x_scaled].b = 255;
//		}
//		
//
//		pos++;
//	}
//
//}
//
