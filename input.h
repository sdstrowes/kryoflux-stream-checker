#ifndef __fluxstream_input_h__
#define __fluxstream_input_h__

#include <stdint.h>

int parse_buffers_from_raw(char *fn, struct track *track, uint8_t side, uint8_t track_num);

#endif

