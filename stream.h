#include <stdint.h>

#ifndef __stream_h__
#define __stream_h__


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
};

#define PASS_COUNT_DEFAULT 5
struct stream_stats {
	uint16_t pass_count_max;
	double *error_rate;
};

int  parse_stream(char *, struct track *, uint8_t side, uint8_t track);
int  decode_track(struct track *);
void dump_stream(struct track *);
void free_stream(struct track *);

#endif

