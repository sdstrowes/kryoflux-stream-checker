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

#include "queue.h"

#define TRACK_MAX 84
#define SIDES      2


void print_help(char *binary_name)
{
	printf("usage: %s -n basename\n", binary_name);
}

struct track_data {
	struct track t;
	STAILQ_ENTRY(track_data) next;
};
STAILQ_HEAD(side, track_data);

struct disk_streams {
	struct side side[2];
};

int main(int argc, char *argv[])
{
	char c;
	char *fn_prefix = NULL;
	int log_level = LOG_INFO;

	opterr = 0;	// silence error output on bad options
	while ((c = getopt (argc, argv, "dn:")) != -1) {
		switch (c) {
		case 'n': {
			fn_prefix = optarg;
			break;
		}
		case 'd': {
			log_level = LOG_DEBUG;
		}
		}
	}

	if (fn_prefix == NULL) {
		print_help(argv[0]);
		exit(1);
	}

	log_init("", log_level);

	struct disk_streams disk;
	STAILQ_INIT(&disk.side[0]);
	STAILQ_INIT(&disk.side[1]);

	int track_num;
	int side;
	char *fn;
	for (track_num = 0; track_num < TRACK_MAX; track_num++) {
		for (side = 0; side < SIDES; side++) {
			struct track_data *track = malloc(sizeof(struct track_data));
			fn = (char *)malloc(strlen(fn_prefix) + 8 + 1);
			sprintf(fn, "%s%02u.%u.raw", fn_prefix, track_num, side);
			int rc = parse_stream(fn, &track->t, side, track_num);
			if (!rc) {
				log_dbg("Loaded %s", fn);
				STAILQ_INSERT_TAIL(&disk.side[side], track, next);
			}
			else {
				log_dbg("Error reading %s", fn);
				free(track);
			}
			free(fn);
		}
	}

	for (side = 0; side < SIDES; side++) {
		struct track_data *track;
		STAILQ_FOREACH(track, &disk.side[side], next) {
			decode_track(&track->t);
		}
	}

//	FILE *svg_out = test_svg_out();
//	STAILQ_FOREACH(track, &head, next) {
//		plot_track(svg_out, &track->t);
//	}

//	finalise_svg(svg_out);

	for (side = 0; side < SIDES; side++) {
		while (!STAILQ_EMPTY(&disk.side[side])) {
			struct track_data *track = STAILQ_FIRST(&disk.side[side]);
			STAILQ_REMOVE_HEAD(&disk.side[side], next);
			free(track);
		}
	}

	return 0;
}

