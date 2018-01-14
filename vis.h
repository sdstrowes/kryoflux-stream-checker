
#include "stream.h"

#ifndef __vis_h__
#define __vis_h__

struct colour {
	uint8_t err;
        uint8_t r;
        uint8_t g;
        uint8_t b;
};

void plot_track(FILE *svg_out, struct track *track);
int  dump_stream_img(struct colour *, uint16_t, uint16_t);

FILE * test_svg_out();
void finalise_svg(FILE *);

#endif

