#ifndef __DISK_STREAMS_H__
#define __DISK_STREAMS_H__

#include "fluxstream.h"

#define SIDES 2

struct side {
	struct track t[TRACK_MAX];
};

struct disk_streams {
	struct side side[SIDES];
	char *name_prefix;
};

#endif
