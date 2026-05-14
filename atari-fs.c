#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
