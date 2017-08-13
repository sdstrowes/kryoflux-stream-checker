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

	log_init("", log_level);



	STAILQ_HEAD(track_list, track_data) head = STAILQ_HEAD_INITIALIZER(head);
	STAILQ_INIT(&head);

	struct track_data *track;


	if (fn_prefix == NULL) {
		print_help(argv[0]);
		exit(1);
	}

	int track_num;
	int side;
	char *fn;
	for (track_num = 0; track_num < TRACK_MAX; track_num++) {
		for (side = 0; side < SIDES; side++) {
			track = malloc(sizeof(struct track_data));
			fn = (char *)malloc(strlen(fn_prefix) + 8 + 1);
			sprintf(fn, "%s%02u.%u.raw", fn_prefix, track_num, side);
			int rc = parse_stream(fn, &track->t, side, track_num);
			if (!rc) {
				log_dbg("Loaded %s", fn);
				STAILQ_INSERT_TAIL(&head, track, next);
			}
			else {
				log_dbg("Error reading %s", fn);
				free(track);
			}
			free(fn);
		}
	}

	STAILQ_FOREACH(track, &head, next) {
		decode_stream(&track->t);
	}

	FILE *svg_out = test_svg_out();
	STAILQ_FOREACH(track, &head, next) {
		plot_track(svg_out, &track->t);
	}

	finalise_svg(svg_out);

	while (!STAILQ_EMPTY(&head)) {
		track = STAILQ_FIRST(&head);
		STAILQ_REMOVE_HEAD(&head, next);
		free(track);
	}

	return 0;
}

