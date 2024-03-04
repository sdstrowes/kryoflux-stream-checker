#ifndef __KF_OOH_H_
#define __KF_OOB_H_

#include "fluxstream.h"
#include <stdio.h>
#include <stdint.h>

int parse_oob(FILE *f, struct track *track, uint32_t *stream_pos);

#endif

