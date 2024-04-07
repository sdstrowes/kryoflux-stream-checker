#include <stdint.h>
#include <sys/queue.h>

#ifndef __fluxstream_h__
#define __fluxstream_h__


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

typedef uint32_t flux_t;


#define STREAM_BUFFER_SIZE 4194304
#define STREAM_RECENT_WINDOW 48
struct bytestream
{
	uint8_t stream[STREAM_BUFFER_SIZE];
	uint8_t recent[STREAM_RECENT_WINDOW];
	double  time_idx[STREAM_BUFFER_SIZE];
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

	double start_ms;
	double end_ms;
};

struct sector_pass {
	uint8_t *data;
	uint16_t data_len;
	uint16_t calc_crc;
	uint16_t disk_crc;

	double start_ms;
	double end_ms;
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

#define PASS_COUNT_DEFAULT 5
struct bytestream_stats {
	uint16_t pass_count_max;
	double *error_rate;
};

struct sector_array {
	struct sector sector[MAX_SECTORS];
	//struct sector_meta meta[10];
	//struct sector_pass sector[10];
};

struct track_array {
	struct sector_array track[81];
};


// disk.side[0..1].track[0..81].sector[0..9].meta/sector
struct disk {
	struct track_array side[2];
};


struct track {
	double   master_clock;
	double   sample_clock;
	double   index_clock;

	uint16_t sample_counter;
	uint32_t index_counter;

	uint8_t side;
	uint8_t track;

	// constructed from OOB data
	struct index *indices;
	// buffer should be the ISB
	uint32_t     *stream_buf;
	// this is the parsed sample stream from the ISB
	uint16_t     *sample_stream;

	uint32_t     indices_idx;
	uint32_t     indices_max;
	uint32_t     stream_buf_idx;
	uint32_t     stream_buf_max;

	struct sector sector[MAX_SECTORS];
	LIST_HEAD(sector_list, sector) sectors;

	struct bytestream *stream;
	struct bytestream_stats stats;
};


int  parse_flux_stream(char *, struct track *, uint8_t side, uint8_t track);
int  decode_flux(struct track *);
void dump_stream(struct track *);
void free_stream(struct track *);

#endif

