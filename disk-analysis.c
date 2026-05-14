#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "atari-fs.h"
#include "disk-analysis-log.h"
#include "fluxstream.h"
#include "input.h"
#include "mfm.h"

#include <sys/queue.h>

#define SIDES      2

void print_help(char *binary_name)
{
	printf("%s:\n", binary_name);
	printf("Required:\n");
	printf(" -n <prefix>: common prefix for flux trace filenames\n");
	printf("Optional:\n");
	printf(" -d: enable debug\n");
	printf(" -h: this help\n");
	printf(" -o <dir>: extract files to directory\n");
}

//struct track_data {
//	struct track t;
//	STAILQ_ENTRY(track_data) next;
//};
//STAILQ_HEAD(side, track_data);

struct side {
	struct track t[TRACK_MAX];
};

struct disk_streams {
	struct side side[SIDES];
	char *name_prefix;
};

void init_struct_disk(struct disk_streams *disk, char *name_prefix)
{
//	STAILQ_INIT(&disk->side[0]);
//	STAILQ_INIT(&disk->side[1]);

	int s, t;
	for (s = 0; s < SIDES; s++) {
		for (t = 0; t < TRACK_MAX; t++) {
			memset(&disk->side[s].t[t], 0, sizeof(struct track));
		}
	}
	//memset(&disk->side[0])

	disk->name_prefix = name_prefix;
}

void free_struct_disk(struct disk_streams *disk)
{
	(void)disk;
	int side;
	for (side = 0; side < SIDES; side++) {
//		while (!STAILQ_EMPTY(&disk->side[side])) {
//			struct track_data *track = STAILQ_FIRST(&disk->side[side]);
//
//			while (!LIST_EMPTY(&(track->t.sectors))) {
//				struct sector *sector = LIST_FIRST(&(track->t.sectors));
//				LIST_REMOVE(sector, next);
//				free(sector->data.data);
//				free(sector);
//			}
//
//			free_stream(&(track->t));
//
//			STAILQ_REMOVE_HEAD(&disk->side[side], next);
//			free(track);
//		}
	}
}

char *construct_filename(char *prefix, int side, int track)
{
	// this should be precisely the max, per the sprintf() format string
	int buffer_size = strlen(prefix) + 8 + 1;

	char *fn = (char *)malloc(buffer_size);
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

int check_index_information(struct track *track, int side, int track_num)
{
	uint32_t i;
	for (i = 0; i + 1 < track->indices_idx; i++) {
		struct index *this_index = &track->index[i];
		struct index *next_index = &track->index[i+1];
		log_dbg("### i:%u, side:%u, track:%u: index: stream_pos:%u, sample_counter:%u, index_counter:%u",
			i, side, track_num,
			this_index->stream_pos,
			this_index->sample_counter,
			this_index->index_counter);

		log_dbg("### starting stream_pos:%x, ending_stream_pos:%x", this_index->stream_pos, next_index->stream_pos);
//		uint32_t s;
//		for (s = this_index->stream_pos; s < next_index->stream_pos; s++) {
//			printf("%02x %08x %04x %04x\n", i, s, track->isb[s], track->flux_buffer[s].val);
//		}

	}

	return 0;

}

//void parse_flux_values_from_isb(struct disk_streams *disk)
//{
//	int track;
//	int side;
//
//	for (side = 0; side < SIDES; side++) {
//		for (track = 0; track < TRACK_MAX; track++) {
//			int rc = parse_buffers_from_raw(&disk->side[side].t[track], side, track);
//			if (!rc) {
//				decode_flux(&disk->side[side].t[track]);
//				//STAILQ_INSERT_TAIL(&disk->side[side], track_data, next);
//			}
//			else {
//				log_dbg("Error parsing ISB from side:%u, track:%02d", side, track);
//				//free(track_data);
//			}
//		}
//	}
//}

void parse_buffers_from_files(struct disk_streams *disk)
{
	int track;
	int side;

	for (side = 0; side < SIDES; side++) {
		for (track = 0; track < TRACK_MAX; track++) {
			//struct track_data *track_data = malloc(sizeof(struct track_data));
			struct track *track_data = &disk->side[side].t[track];

			char *fn = construct_filename(disk->name_prefix, side, track);

			int rc = parse_buffers_from_raw(fn, track_data, side, track);
			if (!rc) {
				log_dbg("Loaded %s", fn);
				decode_flux(track_data);
				//STAILQ_INSERT_TAIL(&disk->side[side], track_data, next);
				check_index_information(track_data, side, track);
			}
			else {
				log_dbg("Error reading %s", fn);
			}

			free(fn);
		}
	}
}


void parse_atari_mfm_from_flux(struct disk_streams *disk, struct disk *disk_data)
{
	int s, t;
	for (s = 0; s < SIDES; s++) {
		for (t = 0; t < TRACK_MAX; t++) {
			decode_flux_to_mfm(disk_data, &disk->side[s].t[t]);
		}
	}
}

void consolidate_sectors(struct disk_streams *disk_streams, struct disk *disk_data)
{
	int s, t;
	for (s = 0; s < SIDES; s++) {
		for (t = 0; t < TRACK_MAX; t++) {
			struct track *track = &disk_streams->side[s].t[t];
			struct sector *sector;
			LIST_FOREACH(sector, &track->sectors, next) {
				if (sector->meta.calc_crc != sector->meta.disk_crc)
					continue;
				if (sector->data.calc_crc != sector->data.disk_crc)
					continue;

				int sec_idx = sector->meta.sector_num - 1;
				int trk_idx = sector->meta.track;
				int sid_idx = sector->meta.side;

				if (sid_idx < 0 || sid_idx >= SIDES)
					continue;
				if (trk_idx < 0 || trk_idx >= TRACK_MAX)
					continue;
				if (sec_idx < 0 || sec_idx >= MAX_SECTORS)
					continue;

				struct sector *dst = &disk_data->side[sid_idx].track[trk_idx].sector[sec_idx];
				if (dst->data.data != NULL)
					continue;

				dst->meta = sector->meta;
				dst->data = sector->data;
				sector->data.data = NULL;
			}
		}
	}
}

static int count_decoded_sectors(struct disk *disk_data)
{
	int count = 0;
	int s, t, sec;
	for (s = 0; s < 2; s++)
		for (t = 0; t < TRACK_MAX; t++)
			for (sec = 0; sec < MAX_SECTORS; sec++)
				if (disk_data->side[s].track[t].sector[sec].data.data != NULL)
					count++;
	return count;
}

int main(int argc, char *argv[])
{
	char c;
	char *fn_prefix = NULL;
	char *out_dir   = NULL;
	int log_level = LOG_INFO;

	opterr = 0;	// silence error output on bad options
	while ((c = getopt(argc, argv, "dhn:o:")) != -1) {
		switch (c) {
		case 'n': {
			fn_prefix = optarg;
			break;
		}
		case 'o': {
			out_dir = optarg;
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
	memset(&disk_data, 0, sizeof(disk_data));

	struct disk_streams disk;
	init_struct_disk(&disk, fn_prefix);

	parse_buffers_from_files(&disk);

	//parse_buffers_from_files(&disk);

	parse_atari_mfm_from_flux(&disk, &disk_data);

	consolidate_sectors(&disk, &disk_data);

	struct bpb bpb;
	if (parse_boot_sector(&disk_data, &bpb) != 0) {
		int decoded = count_decoded_sectors(&disk_data);
		if (decoded > 0)
			printf("Disk has %d readable MFM sectors but no valid boot sector.\n"
			       "This may be a copy-protected disk; no filesystem data to display.\n",
			       decoded);
		else
			printf("No readable sectors found on disk.\n");
		return 1;
	}
	print_bpb(&bpb);

	struct fat fat;
	if (read_fat(&disk_data, &bpb, &fat) != 0) {
		log_err("Failed to read FAT");
		return 1;
	}
	print_fat_summary(&fat);
	print_directory_tree(&disk_data, &bpb, &fat);

	if (out_dir != NULL) {
		if (extract_files(&disk_data, &bpb, &fat, out_dir) == 0)
			printf("Extracted to: %s\n", out_dir);
	}

	free_fat(&fat);

	free_struct_disk(&disk);


	return 0;
}

