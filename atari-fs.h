#ifndef __ATARI_FS_H__
#define __ATARI_FS_H__

#include <stdint.h>

#include "fluxstream.h"

struct fat {
	uint16_t *entries;
	uint16_t  num_entries;
};

struct bpb {
	uint8_t  oem[8];              /* 0x03, 8 bytes, often not meaningful */
	uint16_t bytes_per_sector;    /* 0x0B */
	uint8_t  sectors_per_cluster; /* 0x0D */
	uint16_t reserved_sectors;    /* 0x0E */
	uint8_t  num_fats;            /* 0x10 */
	uint16_t root_dir_entries;    /* 0x11 */
	uint16_t total_sectors;       /* 0x13 */
	uint8_t  media_byte;          /* 0x15 */
	uint16_t sectors_per_fat;     /* 0x16 */
	uint16_t sectors_per_track;   /* 0x18 */
	uint16_t num_sides;           /* 0x1A */
	uint16_t hidden_sectors;      /* 0x1C */

	/* derived — computed from the above, not read from disk */
	uint16_t fat1_sector;
	uint16_t fat2_sector;
	uint16_t root_dir_sector;
	uint16_t root_dir_sectors;
	uint16_t data_sector;
};

int            parse_boot_sector(struct disk *disk_data, struct bpb *out);
void           print_bpb(struct bpb *bpb);
void           check_sector_completeness(struct disk *disk_data, struct bpb *bpb);
struct sector *get_logical_sector(struct disk *disk_data, struct bpb *bpb, uint16_t lsn);

int      read_fat(struct disk *disk_data, struct bpb *bpb, struct fat *out);
void     free_fat(struct fat *fat);
uint16_t fat_next_cluster(struct fat *fat, uint16_t cluster);
int      fat_is_end_of_chain(uint16_t cluster);
uint16_t fat_cluster_to_lsn(struct bpb *bpb, uint16_t cluster);
void     print_fat_summary(struct fat *fat);
void     print_directory_tree(struct disk *disk_data, struct bpb *bpb, struct fat *fat);
int      extract_files(struct disk *disk_data, struct bpb *bpb, struct fat *fat, const char *out_dir);

#endif
