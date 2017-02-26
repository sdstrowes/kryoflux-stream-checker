
#include "stream.h"

#ifndef __vis_h__
#define __vis_h__

struct colour {
        uint8_t r;
        uint8_t g;
        uint8_t b;
};

void plot_track(struct track *, struct colour *, uint16_t, uint16_t);
int  dump_stream_img(struct colour *, uint16_t, uint16_t);

#endif

