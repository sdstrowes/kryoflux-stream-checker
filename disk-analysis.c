#include <cglm/vec3.h>
#include <getopt.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "disk-analysis-log.h"
#include "mfm.h"
#include "stream.h"

#include "queue.h"

#define TRACK_MAX 84
#define SIDES      2

#include "visgl.h"

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
			decode_flux(&track->t);
		}
	}
	for (side = 0; side < SIDES; side++) {
		struct track_data *track;
		STAILQ_FOREACH(track, &disk.side[side], next) {
			decode_flux_to_mfm(&track->t);
		}
	}

	for (side = 0; side < SIDES; side++) {
		struct track_data *track;
		STAILQ_FOREACH(track, &disk.side[side], next) {
			struct track  *t = &track->t;
			struct sector *sector;

			int i;

			int counts[MAX_SECTORS];
			uint16_t sector_hdr_crc[MAX_SECTORS];
			uint16_t sector_dat_crc[MAX_SECTORS];
			for (i = 0; i < MAX_SECTORS; i++) {
				counts[i] = 0;
			}

			LIST_FOREACH(sector, &(t->sectors), next) {
				if (sector->meta.calc_crc == sector->meta.disk_crc &&
				    sector->data.calc_crc == sector->data.disk_crc) {
					if (counts[sector->meta.sector_num-1] == 0) {
						sector_hdr_crc[sector->meta.sector_num-1] = sector->meta.disk_crc;
						sector_dat_crc[sector->meta.sector_num-1] = sector->data.disk_crc;
						counts[sector->meta.sector_num-1]++;
					}
					else {
						if (sector_hdr_crc[sector->meta.sector_num-1] == sector->meta.disk_crc &&
						    sector_dat_crc[sector->meta.sector_num-1] == sector->data.disk_crc) {
								counts[sector->meta.sector_num-1]++;
						}
					}
				}

			}

			for (i = 0; i < MAX_SECTORS; i++) {
				log_msg("side:%u track:%02u sector:%02u: %02u good reads", track->t.side, track->t.track, i, counts[i]);
			}
		}
	}

	int points_count =    0;
	int points_max   = 1024;  // FIXME arbitrary start point
	//GLfloat *points = (GLfloat *)calloc(1, sizeof(GLfloat)*3*points_max);
	//GLfloat *colors = (GLfloat *)calloc(1, sizeof(GLfloat)*3*points_max);

	vec3 *points = (vec3 *)calloc(1, sizeof(vec3)*points_max);
	vec3 *colors = (vec3 *)calloc(1, sizeof(vec3)*points_max);

//	int i = 0;
	for (side = 1; side < SIDES; side++) {
		struct track_data *track;
		STAILQ_FOREACH(track, &disk.side[side], next) {
			build_data_buffers(&track->t, &points, &colors, &points_count, &points_max);
//			i++;
//			if (i > 3) {
//				break;
//			}
		}
	}

	int rc;
//	struct gl_state s;
//	rc = glvis_init(&s);
//	rc = glvis_paint(&s, points, points_count, colors, points_count, true, "/tmp/test.png");
//	rc = glvis_destroy(&s);

	free(points);
	free(colors);

//	int i = 0;
//	struct viscairo *img_out = viscairo_init();
//	for (side = 1; side < SIDES; side++) {
//		struct track_data *track;
//		STAILQ_FOREACH(track, &disk.side[side], next) {
//			plot_track(img_out, &track->t);
//			printf("SIDE %d\n", side);
////			i++;
////			if (i > 1) {
////				break;
////			}
//		}
//	}
//	viscairo_destroy(&img_out);

//	FILE *svg_out = test_svg_out();
//	STAILQ_FOREACH(track, &head, next) {
//		plot_track(svg_out, &track->t);
//	}



	for (side = 0; side < SIDES; side++) {
		while (!STAILQ_EMPTY(&disk.side[side])) {
			struct track_data *track = STAILQ_FIRST(&disk.side[side]);

			while (!LIST_EMPTY(&(track->t.sectors))) {
				struct sector *sector = LIST_FIRST(&(track->t.sectors));
				LIST_REMOVE(sector, next);
				free(sector->data.data);
				free(sector);
			}

			free_stream(&(track->t));

			STAILQ_REMOVE_HEAD(&disk.side[side], next);
			free(track);
		}
	}

	return 0;
}

