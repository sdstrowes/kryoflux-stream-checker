#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "atari-fs.h"
#include "disk-analysis-log.h"
#include "fluxstream.h"

static uint16_t read_le16(uint8_t *buf, int offset)
{
	return (uint16_t)(buf[offset]) | ((uint16_t)(buf[offset + 1]) << 8);
}

int parse_boot_sector(struct disk *disk_data, struct bpb *out)
{
	uint8_t *b = disk_data->side[0].track[0].sector[0].data.data;
	if (b == NULL) {
		log_err("Boot sector (side 0, track 0, sector 1) was not decoded");
		return -1;
	}

	memcpy(out->oem, b + 0x03, 8);
	out->bytes_per_sector    = read_le16(b, 0x0B);
	out->sectors_per_cluster = b[0x0D];
	out->reserved_sectors    = read_le16(b, 0x0E);
	out->num_fats            = b[0x10];
	out->root_dir_entries    = read_le16(b, 0x11);
	out->total_sectors       = read_le16(b, 0x13);
	out->media_byte          = b[0x15];
	out->sectors_per_fat     = read_le16(b, 0x16);
	out->sectors_per_track   = read_le16(b, 0x18);
	out->num_sides           = read_le16(b, 0x1A);
	out->hidden_sectors      = read_le16(b, 0x1C);

	int ok = 1;
	if (out->bytes_per_sector != 512) {
		log_err("BPB: bytes_per_sector is %u, expected 512", out->bytes_per_sector);
		ok = 0;
	}
	if (out->sectors_per_cluster == 0) {
		log_err("BPB: sectors_per_cluster is 0");
		ok = 0;
	}
	if (out->num_fats < 1 || out->num_fats > 2) {
		log_err("BPB: num_fats is %u, expected 1 or 2", out->num_fats);
		ok = 0;
	}
	if (out->root_dir_entries == 0) {
		log_err("BPB: root_dir_entries is 0");
		ok = 0;
	}
	if (out->sectors_per_fat == 0) {
		log_err("BPB: sectors_per_fat is 0");
		ok = 0;
	}
	if (out->sectors_per_track == 0) {
		log_err("BPB: sectors_per_track is 0");
		ok = 0;
	}
	if (out->num_sides == 0 || out->num_sides > 2) {
		log_err("BPB: num_sides is %u, expected 1 or 2", out->num_sides);
		ok = 0;
	}
	if (!ok) {
		return -1;
	}

	out->fat1_sector     = out->reserved_sectors;
	out->fat2_sector     = out->reserved_sectors + out->sectors_per_fat;
	out->root_dir_sector = out->reserved_sectors + out->num_fats * out->sectors_per_fat;
	out->root_dir_sectors = (uint16_t)((out->root_dir_entries * 32 + out->bytes_per_sector - 1)
	                                   / out->bytes_per_sector);
	out->data_sector     = out->root_dir_sector + out->root_dir_sectors;

	return 0;
}

struct sector *get_logical_sector(struct disk *disk_data, struct bpb *bpb, uint16_t lsn)
{
	if (lsn >= bpb->total_sectors)
		return NULL;

	uint16_t track  = lsn / (bpb->sectors_per_track * bpb->num_sides);
	uint16_t side   = (lsn / bpb->sectors_per_track) % bpb->num_sides;
	uint16_t sector = lsn % bpb->sectors_per_track;

	if (track >= TRACK_MAX || side >= 2 || sector >= MAX_SECTORS)
		return NULL;

	struct sector *s = &disk_data->side[side].track[track].sector[sector];
	if (s->data.data == NULL)
		return NULL;

	return s;
}

int read_fat(struct disk *disk_data, struct bpb *bpb, struct fat *out)
{
	uint32_t fat_bytes = (uint32_t)bpb->sectors_per_fat * bpb->bytes_per_sector;

	uint8_t *buf = calloc(fat_bytes, 1);
	if (buf == NULL)
		return -1;

	for (uint16_t i = 0; i < bpb->sectors_per_fat; i++) {
		struct sector *s = get_logical_sector(disk_data, bpb, bpb->fat1_sector + i);
		if (s == NULL && bpb->num_fats >= 2) {
			log_msg("FAT1: sector %u missing, trying FAT2", bpb->fat1_sector + i);
			s = get_logical_sector(disk_data, bpb, bpb->fat2_sector + i);
		}
		if (s == NULL) {
			log_err("FAT: sector %u missing in all copies, using zeros", bpb->fat1_sector + i);
			memset(buf + i * bpb->bytes_per_sector, 0, bpb->bytes_per_sector);
			continue;
		}
		memcpy(buf + i * bpb->bytes_per_sector, s->data.data, bpb->bytes_per_sector);
	}

	/* FAT12: each entry is 12 bits, so 2 entries per 3 bytes */
	uint16_t num_entries = (uint16_t)((fat_bytes * 2) / 3);

	uint16_t *entries = calloc(num_entries, sizeof(uint16_t));
	if (entries == NULL) {
		free(buf);
		return -1;
	}

	for (uint16_t n = 0; n < num_entries; n++) {
		uint32_t byte_off = ((uint32_t)n * 3) / 2;
		if (n % 2 == 0)
			entries[n] = buf[byte_off] | ((uint16_t)(buf[byte_off + 1] & 0x0F) << 8);
		else
			entries[n] = (buf[byte_off] >> 4) | ((uint16_t)buf[byte_off + 1] << 4);
	}

	free(buf);
	out->entries     = entries;
	out->num_entries = num_entries;
	return 0;
}

void free_fat(struct fat *fat)
{
	free(fat->entries);
	fat->entries     = NULL;
	fat->num_entries = 0;
}

uint16_t fat_next_cluster(struct fat *fat, uint16_t cluster)
{
	if (cluster >= fat->num_entries)
		return 0xFFF;
	return fat->entries[cluster];
}

int fat_is_end_of_chain(uint16_t cluster)
{
	return cluster >= 0xFF8;
}

uint16_t fat_cluster_to_lsn(struct bpb *bpb, uint16_t cluster)
{
	return bpb->data_sector + (cluster - 2) * bpb->sectors_per_cluster;
}

void print_fat_summary(struct fat *fat)
{
	uint16_t free_count = 0, used_count = 0, bad_count = 0;

	/* entries 0 and 1 are reserved; data clusters start at 2 */
	for (uint16_t n = 2; n < fat->num_entries; n++) {
		uint16_t v = fat->entries[n];
		if (v == 0x000)
			free_count++;
		else if (v == 0xFF7)
			bad_count++;
		else
			used_count++;
	}

	printf("FAT:\n");
	printf("  entry[0] (media):  0x%03x\n", fat->entries[0]);
	printf("  entry[1] (eoc):    0x%03x\n", fat->entries[1]);
	printf("  data clusters:     %u total, %u used, %u free, %u bad\n",
	       fat->num_entries - 2, used_count, free_count, bad_count);
}

#define DIRENT_SIZE   32
#define ATTR_READONLY 0x01
#define ATTR_HIDDEN   0x02
#define ATTR_SYSTEM   0x04
#define ATTR_VOLUME   0x08
#define ATTR_SUBDIR   0x10
#define ATTR_ARCHIVE  0x20
#define MAX_DIR_DEPTH 16

static void format_attrs(uint8_t attr, char *out)
{
	out[0] = (attr & ATTR_READONLY) ? 'R' : '-';
	out[1] = (attr & ATTR_HIDDEN)   ? 'H' : '-';
	out[2] = (attr & ATTR_SYSTEM)   ? 'S' : '-';
	out[3] = (attr & ATTR_VOLUME)   ? 'V' : '-';
	out[4] = (attr & ATTR_SUBDIR)   ? 'D' : '-';
	out[5] = (attr & ATTR_ARCHIVE)  ? 'A' : '-';
	out[6] = '\0';
}

static void format_timestamp(uint8_t *e, char *out)
{
	uint16_t time = (uint16_t)e[22] | ((uint16_t)e[23] << 8);
	uint16_t date = (uint16_t)e[24] | ((uint16_t)e[25] << 8);

	if (date == 0) {
		snprintf(out, 17, "                ");
		return;
	}

	int year  = ((date >> 9) & 0x7F) + 1980;
	int month = (date >> 5) & 0x0F;
	int day   = date & 0x1F;
	int hours = (time >> 11) & 0x1F;
	int mins  = (time >> 5) & 0x3F;

	snprintf(out, 17, "%04d-%02d-%02d %02d:%02d", year, month, day, hours, mins);
}

static void format_83_name(uint8_t *raw, char *out)
{
	int i, nlen = 0, elen = 0;
	char name[9], ext[4];

	/* first byte 0x05 means the real first character is 0xE5 */
	uint8_t first = (raw[0] == 0x05) ? 0xE5 : raw[0];
	uint8_t tmp[11];
	tmp[0] = first;
	memcpy(tmp + 1, raw + 1, 10);

	for (i = 7; i >= 0; i--) {
		if (tmp[i] != ' ') { nlen = i + 1; break; }
	}
	for (i = 2; i >= 0; i--) {
		if (tmp[8 + i] != ' ') { elen = i + 1; break; }
	}

	memcpy(name, tmp, nlen);
	name[nlen] = '\0';
	memcpy(ext, tmp + 8, elen);
	ext[elen] = '\0';

	if (elen > 0)
		snprintf(out, 13, "%s.%s", name, ext);
	else
		snprintf(out, 13, "%s", name);
}

static uint8_t *read_dir_data(struct disk *disk_data, struct bpb *bpb, struct fat *fat,
                               uint16_t first_cluster, int is_root, uint32_t *out_size)
{
	uint32_t size;
	uint8_t *buf;

	if (is_root) {
		size = (uint32_t)bpb->root_dir_sectors * bpb->bytes_per_sector;
		buf = calloc(size, 1);
		if (!buf)
			return NULL;
		for (uint16_t i = 0; i < bpb->root_dir_sectors; i++) {
			struct sector *s = get_logical_sector(disk_data, bpb,
			                                      bpb->root_dir_sector + i);
			if (s)
				memcpy(buf + (uint32_t)i * bpb->bytes_per_sector,
				       s->data.data, bpb->bytes_per_sector);
		}
	} else {
		uint32_t cluster_size = (uint32_t)bpb->sectors_per_cluster * bpb->bytes_per_sector;
		uint32_t num_clusters = 0;
		uint16_t cluster = first_cluster;
		while (cluster >= 2 && !fat_is_end_of_chain(cluster) &&
		       num_clusters < fat->num_entries) {
			num_clusters++;
			cluster = fat_next_cluster(fat, cluster);
		}
		size = num_clusters * cluster_size;
		if (size == 0)
			return NULL;
		buf = calloc(size, 1);
		if (!buf)
			return NULL;

		cluster = first_cluster;
		uint32_t off = 0;
		while (cluster >= 2 && !fat_is_end_of_chain(cluster)) {
			uint16_t lsn = fat_cluster_to_lsn(bpb, cluster);
			for (uint8_t i = 0; i < bpb->sectors_per_cluster; i++) {
				struct sector *s = get_logical_sector(disk_data, bpb, lsn + i);
				if (s)
					memcpy(buf + off, s->data.data, bpb->bytes_per_sector);
				off += bpb->bytes_per_sector;
			}
			cluster = fat_next_cluster(fat, cluster);
		}
	}

	*out_size = size;
	return buf;
}

static void list_dir(struct disk *disk_data, struct bpb *bpb, struct fat *fat,
                     uint8_t *buf, uint32_t size, int depth)
{
	char indent[MAX_DIR_DEPTH * 2 + 1];
	int ind = (depth * 2 < (int)sizeof(indent) - 1) ? depth * 2 : (int)sizeof(indent) - 1;
	memset(indent, ' ', ind);
	indent[ind] = '\0';

	uint32_t num_entries = size / DIRENT_SIZE;
	for (uint32_t i = 0; i < num_entries; i++) {
		uint8_t *e = buf + i * DIRENT_SIZE;

		if (e[0] == 0x00)
			break;
		if (e[0] == 0xE5)
			continue;

		uint8_t attr = e[11];
		if (attr & ATTR_VOLUME)
			continue;

		char name[13];
		format_83_name(e, name);

		if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
			continue;

		uint16_t first_cluster = (uint16_t)e[26] | ((uint16_t)e[27] << 8);
		uint32_t file_size     = (uint32_t)e[28] | ((uint32_t)e[29] << 8) |
		                         ((uint32_t)e[30] << 16) | ((uint32_t)e[31] << 24);

		char attrs[7];
		format_attrs(attr, attrs);

		char ts[17];
		format_timestamp(e, ts);

		char fullname[15];
		if (attr & ATTR_SUBDIR)
			snprintf(fullname, sizeof(fullname), "%s/", name);
		else
			snprintf(fullname, sizeof(fullname), "%s", name);

		if (attr & ATTR_SUBDIR) {
			printf("%s%-13s  %s  %s\n", indent, fullname, attrs, ts);
			if (depth < MAX_DIR_DEPTH - 1 && first_cluster >= 2) {
				uint32_t sub_size;
				uint8_t *sub_buf = read_dir_data(disk_data, bpb, fat,
				                                 first_cluster, 0, &sub_size);
				if (sub_buf) {
					list_dir(disk_data, bpb, fat, sub_buf, sub_size, depth + 1);
					free(sub_buf);
				}
			}
		} else {
			printf("%s%-13s  %s  %s  %7u bytes\n", indent, fullname, attrs, ts, file_size);
		}
	}
}

void print_directory_tree(struct disk *disk_data, struct bpb *bpb, struct fat *fat)
{
	printf("Files:\n");
	uint32_t size;
	uint8_t *buf = read_dir_data(disk_data, bpb, fat, 0, 1, &size);
	if (!buf) {
		log_err("Failed to read root directory");
		return;
	}
	list_dir(disk_data, bpb, fat, buf, size, 1);
	free(buf);
}

static int extract_file(struct disk *disk_data, struct bpb *bpb, struct fat *fat,
                         uint16_t first_cluster, uint32_t file_size,
                         const char *out_path)
{
	FILE *f = fopen(out_path, "wb");
	if (!f) {
		log_err("Cannot create %s: %s", out_path, strerror(errno));
		return -1;
	}

	uint32_t remaining = file_size;
	uint16_t cluster = first_cluster;
	static const uint8_t zeros[512];

	while (remaining > 0 && cluster >= 2 && !fat_is_end_of_chain(cluster)) {
		uint16_t lsn = fat_cluster_to_lsn(bpb, cluster);
		for (uint8_t s = 0; s < bpb->sectors_per_cluster && remaining > 0; s++) {
			uint32_t to_write = remaining < bpb->bytes_per_sector
			                    ? remaining : bpb->bytes_per_sector;
			struct sector *sec = get_logical_sector(disk_data, bpb, lsn + s);
			if (sec)
				fwrite(sec->data.data, 1, to_write, f);
			else {
				log_err("Missing sector at LSN %u, writing zeros", lsn + s);
				fwrite(zeros, 1, to_write, f);
			}
			remaining -= to_write;
		}
		cluster = fat_next_cluster(fat, cluster);
	}

	fclose(f);
	return 0;
}

static void extract_dir(struct disk *disk_data, struct bpb *bpb, struct fat *fat,
                         uint8_t *buf, uint32_t size, const char *out_path)
{
	uint32_t num_entries = size / DIRENT_SIZE;
	for (uint32_t i = 0; i < num_entries; i++) {
		uint8_t *e = buf + i * DIRENT_SIZE;

		if (e[0] == 0x00)
			break;
		if (e[0] == 0xE5)
			continue;

		uint8_t attr = e[11];
		if (attr & ATTR_VOLUME)
			continue;

		char name[13];
		format_83_name(e, name);

		if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
			continue;

		char entry_path[PATH_MAX];
		snprintf(entry_path, sizeof(entry_path), "%s/%s", out_path, name);

		uint16_t first_cluster = (uint16_t)e[26] | ((uint16_t)e[27] << 8);
		uint32_t file_size     = (uint32_t)e[28] | ((uint32_t)e[29] << 8) |
		                         ((uint32_t)e[30] << 16) | ((uint32_t)e[31] << 24);

		if (attr & ATTR_SUBDIR) {
			if (mkdir(entry_path, 0755) != 0 && errno != EEXIST) {
				log_err("Cannot create directory %s: %s", entry_path, strerror(errno));
				continue;
			}
			if (first_cluster >= 2) {
				uint32_t sub_size;
				uint8_t *sub_buf = read_dir_data(disk_data, bpb, fat,
				                                 first_cluster, 0, &sub_size);
				if (sub_buf) {
					extract_dir(disk_data, bpb, fat, sub_buf, sub_size, entry_path);
					free(sub_buf);
				}
			}
		} else {
			extract_file(disk_data, bpb, fat, first_cluster, file_size, entry_path);
		}
	}
}

int extract_files(struct disk *disk_data, struct bpb *bpb, struct fat *fat,
                   const char *out_dir)
{
	if (mkdir(out_dir, 0755) != 0 && errno != EEXIST) {
		log_err("Cannot create output directory %s: %s", out_dir, strerror(errno));
		return -1;
	}

	uint32_t size;
	uint8_t *buf = read_dir_data(disk_data, bpb, fat, 0, 1, &size);
	if (!buf) {
		log_err("Failed to read root directory for extraction");
		return -1;
	}
	extract_dir(disk_data, bpb, fat, buf, size, out_dir);
	free(buf);
	return 0;
}

void print_bpb(struct bpb *bpb)
{
	char oem_printable[9];
	int i;
	for (i = 0; i < 8; i++) {
		uint8_t c = bpb->oem[i];
		oem_printable[i] = (c >= 32 && c < 127) ? (char)c : '.';
	}
	oem_printable[8] = '\0';

	printf("Boot sector:\n");
	printf("  OEM name:          ");
	for (i = 0; i < 8; i++) printf("%02x", bpb->oem[i]);
	printf("  \"%s\"\n", oem_printable);
	printf("  bytes/sector:      %u\n",   bpb->bytes_per_sector);
	printf("  sectors/cluster:   %u\n",   bpb->sectors_per_cluster);
	printf("  reserved sectors:  %u\n",   bpb->reserved_sectors);
	printf("  FATs:              %u\n",   bpb->num_fats);
	printf("  root dir entries:  %u\n",   bpb->root_dir_entries);
	printf("  total sectors:     %u\n",   bpb->total_sectors);
	printf("  media byte:        0x%02x\n", bpb->media_byte);
	printf("  sectors/FAT:       %u\n",   bpb->sectors_per_fat);
	printf("  sectors/track:     %u\n",   bpb->sectors_per_track);
	printf("  sides:             %u\n",   bpb->num_sides);
	printf("  hidden sectors:    %u\n",   bpb->hidden_sectors);
	printf("Derived layout:\n");
	printf("  FAT 1:             sector %u\n", bpb->fat1_sector);
	printf("  FAT 2:             sector %u\n", bpb->fat2_sector);
	printf("  root directory:    sector %u  (%u sectors)\n",
	       bpb->root_dir_sector, bpb->root_dir_sectors);
	printf("  data area:         sector %u\n", bpb->data_sector);
}
