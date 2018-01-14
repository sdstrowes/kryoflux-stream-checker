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

