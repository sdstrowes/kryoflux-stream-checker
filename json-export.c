#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/queue.h>

#include "disk-analysis-log.h"
#include "json-export.h"

#define FLUX_OVERVIEW_BUCKETS 360

static void write_json_string(FILE *f, const char *s)
{
	fputc('"', f);
	if (s != NULL) {
		const unsigned char *p;
		for (p = (const unsigned char *)s; *p; p++) {
			switch (*p) {
			case '"':  fputs("\\\"", f); break;
			case '\\': fputs("\\\\", f); break;
			case '\n': fputs("\\n", f);  break;
			case '\r': fputs("\\r", f);  break;
			case '\t': fputs("\\t", f);  break;
			default:
				/* Atari 8-bit charset bytes aren't valid UTF-8; escape
				 * anything outside printable ASCII as a \u unicode
				 * codepoint rather than emit invalid JSON. */
				if (*p < 0x20 || *p >= 0x80)
					fprintf(f, "\\u%04x", *p);
				else
					fputc(*p, f);
			}
		}
	}
	fputc('"', f);
}

static char flux_class_char(double flux_us)
{
	if (flux_us > 0.0000034 && flux_us < 0.0000046) return '2';
	if (flux_us > 0.0000054 && flux_us < 0.0000066) return '3';
	if (flux_us > 0.0000074 && flux_us < 0.0000086) return '4';
	return 'x';
}

static int bucket_of(double t, double total_time, int buckets)
{
	if (total_time <= 0.0) return 0;
	int b = (int)(t / total_time * buckets);
	if (b < 0) b = 0;
	if (b >= buckets) b = buckets - 1;
	return b;
}

/* Coarse per-bucket flux classification of revolution 0, used to paint the
 * whole-disk overview ring before any per-track detail has been fetched. */
static void compute_flux_overview(struct track *track, char *out)
{
	memset(out, '.', FLUX_OVERVIEW_BUCKETS);

	if (track->flux_buffer == NULL || track->indices_idx < 2)
		return;

	uint32_t flux_start = track->index[0].stream_pos;
	uint32_t flux_end   = track->index[1].stream_pos;
	if (flux_end > track->flux_buf_idx)
		flux_end = track->flux_buf_idx;

	double total_time = 0.0;
	uint32_t i;
	for (i = flux_start; i < flux_end; i++)
		total_time += track->flux_buffer[i].val / track->sample_clock;
	if (total_time <= 0.0)
		return;

	double elapsed = 0.0;
	for (i = flux_start; i < flux_end; i++) {
		double flux_us = track->flux_buffer[i].val / track->sample_clock;
		int b0 = bucket_of(elapsed, total_time, FLUX_OVERVIEW_BUCKETS);
		int b1 = bucket_of(elapsed + flux_us, total_time, FLUX_OVERVIEW_BUCKETS);
		if (b1 <= b0) b1 = b0 + 1;
		if (b1 > FLUX_OVERVIEW_BUCKETS) b1 = FLUX_OVERVIEW_BUCKETS;
		char c = flux_class_char(flux_us);
		int b;
		for (b = b0; b < b1; b++)
			out[b] = c;
		elapsed += flux_us;
	}
}

static const char *region_name(uint8_t region)
{
	switch (region) {
	case SF_BOOT:    return "boot";
	case SF_FAT:     return "fat";
	case SF_ROOTDIR: return "rootdir";
	case SF_FILE:    return "file";
	default:         return "free";
	}
}

static int write_disk_json(struct disk_streams *disk, struct disk *disk_data,
                            struct bpb *bpb, struct fat *fat, const char *out_dir)
{
	char path[4096];
	snprintf(path, sizeof(path), "%s/disk.json", out_dir);

	FILE *f = fopen(path, "w");
	if (f == NULL) {
		log_err("Could not open %s for writing", path);
		return -1;
	}

	static struct sector_file_info fsmap[2][TRACK_MAX][MAX_SECTORS];
	if (bpb != NULL && fat != NULL)
		build_filesystem_map(disk_data, bpb, fat, fsmap);
	else
		memset(fsmap, 0, sizeof(fsmap));

	fprintf(f, "{\n  \"sides\": %d,\n  \"track_max\": %d,\n  \"side\": [\n", SIDES, TRACK_MAX);

	int s, t;
	for (s = 0; s < SIDES; s++) {
		fprintf(f, "    {\n      \"track\": [\n");
		for (t = 0; t < TRACK_MAX; t++) {
			struct track *track = &disk->side[s].t[t];

			uint32_t revolutions = track->indices_idx > 0 ? track->indices_idx - 1 : 0;

			char overview[FLUX_OVERVIEW_BUCKETS + 1];
			compute_flux_overview(track, overview);
			overview[FLUX_OVERVIEW_BUCKETS] = '\0';

			fprintf(f, "        {\n");
			fprintf(f, "          \"num\": %d,\n", t);
			fprintf(f, "          \"revolutions\": %u,\n", revolutions);
			fprintf(f, "          \"sample_clock\": %.6f,\n", track->sample_clock);
			fprintf(f, "          \"flux_overview\": ");
			write_json_string(f, overview);
			fprintf(f, ",\n");

			fprintf(f, "          \"sectors\": [");
			struct sector *sector;
			int first = 1;
			LIST_FOREACH(sector, &track->sectors, next) {
				if (sector->meta.revolution == UINT32_MAX)
					continue;
				if (!first) fprintf(f, ",");
				first = 0;
				int id_ok   = (sector->meta.calc_crc == sector->meta.disk_crc);
				int data_ok = (sector->meta.data_bit_end > sector->meta.data_bit_start) &&
				              (sector->data.calc_crc == sector->data.disk_crc);
				fprintf(f, "\n            {\"num\": %u, \"revolution\": %u, "
				           "\"id_ok\": %s, \"data_ok\": %s, "
				           "\"id_frac0\": %.6f, \"id_frac1\": %.6f, "
				           "\"data_frac0\": %.6f, \"data_frac1\": %.6f}",
				        sector->meta.sector_num, sector->meta.revolution,
				        id_ok ? "true" : "false", data_ok ? "true" : "false",
				        sector->meta.id_frac0, sector->meta.id_frac1,
				        sector->meta.data_frac0, sector->meta.data_frac1);
			}
			fprintf(f, "%s],\n", first ? "" : "\n          ");

			fprintf(f, "          \"filesystem\": [");
			int sec;
			int fs_first = 1;
			for (sec = 0; sec < MAX_SECTORS; sec++) {
				struct sector_file_info *info = &fsmap[s][t][sec];
				int decoded = disk_data->side[s].track[t].sector[sec].data.data != NULL;
				if (info->region == SF_FREE && !decoded)
					continue;
				if (!fs_first) fprintf(f, ",");
				fs_first = 0;
				fprintf(f, "\n            {\"sector\": %d, \"decoded\": %s, \"region\": ",
				        sec + 1, decoded ? "true" : "false");
				write_json_string(f, region_name(info->region));
				fprintf(f, ", \"file\": ");
				if (info->region == SF_FILE || info->region == SF_ROOTDIR)
					write_json_string(f, info->name);
				else
					fprintf(f, "null");
				fprintf(f, "}");
			}
			fprintf(f, "%s]\n", fs_first ? "" : "\n          ");

			fprintf(f, "        }%s\n", (t < TRACK_MAX - 1) ? "," : "");
		}
		fprintf(f, "      ]\n    }%s\n", (s < SIDES - 1) ? "," : "");
	}

	fprintf(f, "  ]\n}\n");

	fclose(f);
	return 0;
}

static int write_track_flux_json(struct track *track, const char *out_dir)
{
	char path[4096];
	snprintf(path, sizeof(path), "%s/flux/%u-%02u.json", out_dir, track->side, track->track);

	FILE *f = fopen(path, "w");
	if (f == NULL) {
		log_err("Could not open %s for writing", path);
		return -1;
	}

	fprintf(f, "{\n  \"side\": %u,\n  \"track\": %u,\n  \"sample_clock\": %.6f,\n  \"revolutions\": [",
	        track->side, track->track, track->sample_clock);

	uint32_t i;
	int first_rev = 1;
	for (i = 0; track->indices_idx > 0 && i + 1 < track->indices_idx; i++) {
		uint32_t flux_start = track->index[i].stream_pos;
		uint32_t flux_end   = track->index[i + 1].stream_pos;
		if (flux_end > track->flux_buf_idx)
			flux_end = track->flux_buf_idx;

		double total_time = 0.0;
		uint32_t j;
		for (j = flux_start; j < flux_end; j++)
			total_time += track->flux_buffer[j].val / track->sample_clock;

		if (!first_rev) fprintf(f, ",");
		first_rev = 0;
		fprintf(f, "\n    {\n      \"total_time\": %.9f,\n      \"flux\": [", total_time);

		int first_val = 1;
		for (j = flux_start; j < flux_end; j++) {
			if (!first_val) fputc(',', f);
			first_val = 0;
			fprintf(f, "%u", track->flux_buffer[j].val);
		}
		fprintf(f, "]\n    }");
	}

	fprintf(f, "%s]\n}\n", first_rev ? "" : "\n  ");

	fclose(f);
	return 0;
}

int export_disk_json(struct disk_streams *disk, struct disk *disk_data,
                      struct bpb *bpb, struct fat *fat, const char *out_dir)
{
	char flux_dir[4096];
	snprintf(flux_dir, sizeof(flux_dir), "%s/flux", out_dir);

	if (mkdir(out_dir, 0755) != 0 && errno != EEXIST) {
		log_err("Could not create %s: %s", out_dir, strerror(errno));
		return -1;
	}
	if (mkdir(flux_dir, 0755) != 0 && errno != EEXIST) {
		log_err("Could not create %s: %s", flux_dir, strerror(errno));
		return -1;
	}

	if (write_disk_json(disk, disk_data, bpb, fat, out_dir) != 0)
		return -1;

	int s, t;
	for (s = 0; s < SIDES; s++) {
		for (t = 0; t < TRACK_MAX; t++) {
			if (write_track_flux_json(&disk->side[s].t[t], out_dir) != 0)
				return -1;
		}
	}

	return 0;
}
