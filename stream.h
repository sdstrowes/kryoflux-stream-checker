#include <stdint.h>
#include "queue.h"

#ifndef __stream_h__
#define __stream_h__


#define SECTOR_BYTES_MAX 4096

/* page 11:
 * When an index signal is detected the information is not placed in the stream
 * buffer but the position of the next flux reversal in the stream buffer is
 * recorded as well as the value of the Sample Counter (time from previous flux
 * reversal) and the Index Counter */
struct index {
	uint32_t stream_pos;
	uint32_t sample_counter;
	uint32_t index_counter;
};

typedef uint16_t flux_t;


#define STREAM_BUFFER_SIZE 4194304
#define STREAM_RECENT_WINDOW 48
struct bytestream
{
	uint8_t stream[STREAM_BUFFER_SIZE];
	uint8_t recent[STREAM_RECENT_WINDOW];
	uint32_t ptr;
	uint8_t subptr;
};


struct sector_meta {
	uint8_t track;
	uint8_t side;
	uint8_t sector_num;
	uint16_t size;
	uint16_t calc_crc;
	uint16_t disk_crc;
};

struct sector_pass {
	uint8_t *data;
	uint16_t data_len;
	uint16_t calc_crc;
	uint16_t disk_crc;
};

struct sector {
	int pass_count;
	struct sector_meta meta;
	struct sector_pass data;

	LIST_ENTRY(sector) next;
};

#define MAX_SECTORS 10

void sector_init(struct sector **s);

void sector_add_pass();

struct track {
	double   master_clock;
	double   sample_clock;
	double   index_clock;
	uint16_t sample_counter;
	uint32_t index_counter;

	uint8_t side;
	uint8_t track;

	struct index *indices;
	uint16_t     *flux_array;

	uint32_t     indices_idx;
	uint32_t     indices_max;
	uint32_t     flux_array_idx;
	uint32_t     flux_array_max;

	struct sector sector[MAX_SECTORS];
	LIST_HEAD(sector_list, sector) sectors;

	struct bytestream *stream;
};

#define PASS_COUNT_DEFAULT 5
struct stream_stats {
	uint16_t pass_count_max;
	double *error_rate;
};

int  parse_stream(char *, struct track *, uint8_t side, uint8_t track);
int  decode_flux(struct track *);
void dump_stream(struct track *);
void free_stream(struct track *);

#endif

