#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "disk-analysis-log.h"
#include "mfm.h"
#include "fluxstream.h"

#include <sys/queue.h>

#define TRACK_MAX 84
#define SIDES      2

void print_help(char *binary_name)
{
	printf("%s:\n", binary_name);
	printf("Required:\n");
	printf(" -n <prefix>: common prefix for flux trace filenames\n");
	printf("Optional:\n");
	printf(" -d: enable debug\n");
	printf(" -h: this help\n");
}

struct track_data {
	struct track t;
	STAILQ_ENTRY(track_data) next;
};
STAILQ_HEAD(side, track_data);

struct disk_streams {
	struct side side[2];
	char *name_prefix;
};

void init_struct_disk(struct disk_streams *disk, char *name_prefix)
{
	STAILQ_INIT(&disk->side[0]);
	STAILQ_INIT(&disk->side[1]);

	disk->name_prefix = name_prefix;
}

void free_struct_disk(struct disk_streams *disk)
{
	int side;
	for (side = 0; side < SIDES; side++) {
		while (!STAILQ_EMPTY(&disk->side[side])) {
			struct track_data *track = STAILQ_FIRST(&disk->side[side]);

			while (!LIST_EMPTY(&(track->t.sectors))) {
				struct sector *sector = LIST_FIRST(&(track->t.sectors));
				LIST_REMOVE(sector, next);
				free(sector->data.data);
				free(sector);
			}

			free_stream(&(track->t));

			STAILQ_REMOVE_HEAD(&disk->side[side], next);
			free(track);
		}
	}
}

char *construct_filename(char *prefix, int side, int track)
{
	// this should be precisely the max, per the sprintf() format string
	int buffer_size = strlen(prefix) + 8 + 1;

	char *fn = (char *)malloc(buffer_size);
	memset(fn, '\0', buffer_size);
	if (fn == NULL) {
		log_err("malloc() failed; \"%s\"", strerror(errno));
		exit(1);
	}

	int rc = snprintf(fn, buffer_size, "%s%02u.%u.raw", prefix, track, side);
	if (rc >= buffer_size) {
		log_err("Filename truncated? \"%s\"", fn);
		log_err("Aborting");
		exit(1);
	}

	return fn;
}


void parse_disk_fluxes_from_files(struct disk_streams *disk)
{
	int track;
	int side;

	for (side = 0; side < SIDES; side++) {
		for (track = 0; track < TRACK_MAX; track++) {
			struct track_data *track_data = malloc(sizeof(struct track_data));

			char *fn = construct_filename(disk->name_prefix, side, track);

			int rc = parse_flux_stream(fn, &track_data->t, side, track);
			if (!rc) {
				log_dbg("Loaded %s", fn);
				decode_flux(&track_data->t);
				STAILQ_INSERT_TAIL(&disk->side[side], track_data, next);
			}
			else {
				log_dbg("Error reading %s", fn);
				free(track_data);
			}

			free(fn);
		}
	}
}

void parse_atari_mfm_from_flux(struct disk_streams *disk, struct disk *disk_data)
{
	int side;
	for (side = 0; side < SIDES; side++) {
		struct track_data *track;
		STAILQ_FOREACH(track, &disk->side[side], next) {
			decode_flux_to_mfm(disk_data, &track->t);
		}
	}
}

int main(int argc, char *argv[])
{
	char c;
	char *fn_prefix = NULL;
	int log_level = LOG_INFO;

	opterr = 0;	// silence error output on bad options
	while ((c = getopt (argc, argv, "dhn:")) != -1) {
		switch (c) {
		case 'n': {
			fn_prefix = optarg;
			break;
		}
		case 'd': {
			log_level = LOG_DEBUG;
			break;
		}
		case 'h': {
			print_help(argv[0]);
			exit(0);
		}
		}
	}

	if (fn_prefix == NULL) {
		print_help(argv[0]);
		exit(1);
	}

	log_init("", log_level);

	struct disk disk_data;

	struct disk_streams disk;
	init_struct_disk(&disk, fn_prefix);

	parse_disk_fluxes_from_files(&disk);

	parse_atari_mfm_from_flux(&disk, &disk_data);

	free_struct_disk(&disk);


	return 0;
}

