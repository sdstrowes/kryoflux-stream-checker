#include <stdint.h>

struct index {
	uint32_t stream_pos;
	uint32_t sample_counter;
	uint32_t index_counter;
};

struct flux {
	uint16_t flux_val;
	uint32_t stream_pos;
};

struct track {
	double   master_clock;
	double   sample_clock;
	double   index_clock;
	uint16_t sample_counter;
	uint32_t index_counter;

	uint8_t side;
	uint8_t track;

	struct index *indices;
	struct flux  *flux_array;

	uint32_t     indices_idx;
	uint32_t     indices_max;
	uint32_t     flux_array_idx;
	uint32_t     flux_array_max;
};

struct colour {
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

int  parse_stream(char *, struct track *, uint8_t side, uint8_t track);
int  decode_stream(struct track *);
void dump_stream(struct track *);
int  vis_stream(struct track *);
void plot_track(struct track *, struct colour *, uint16_t, uint16_t);
void free_stream(struct track *);

