#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mfm.h"
#include "fluxstream.h"
#include "disk-analysis-log.h"

void sector_init(struct sector **s)
{
	struct sector *sector = (struct sector *)malloc(sizeof(struct sector));
	sector->pass_count    = 0;
	memset(&sector->meta, 0, sizeof(struct sector_meta));
	memset(&sector->data, 0, sizeof(struct sector_pass));
	sector->data.data     = (uint8_t *)malloc(512);
	memset(sector->data.data, 0, 512);
	sector->data.data_len = 512;

	*s = sector;
}

void append_index(struct track *track, uint32_t stream_pos, uint32_t sample_counter, uint32_t index_counter)
{
	if (track->indices_idx >= track->indices_max - 1) {
		track->indices_max *= 2;
		track->indices = (struct index *)realloc(track->indices, sizeof(struct index)*track->indices_max);
	}

	track->indices[track->indices_idx].stream_pos     = stream_pos;
	track->indices[track->indices_idx].sample_counter = sample_counter;
	track->indices[track->indices_idx].index_counter  = index_counter;
	track->indices_idx++;
}


void append_flux(struct track *track, uint16_t flux_val, uint32_t stream_pos)
{
	if (stream_pos >= track->flux_array_max - 1) {
		uint32_t old_max = track->flux_array_max;
		track->flux_array_max *= 2;

		flux_t *tmp = (flux_t *)calloc(track->flux_array_max, sizeof(flux_t));
		if (track->flux_array != NULL) {
			memcpy(tmp, track->flux_array, old_max*sizeof(flux_t));
			free(track->flux_array);
		}
		track->flux_array = tmp;
	}

	track->flux_array[stream_pos] = flux_val;

	// idx becomes a marker for the last entry in the array
	track->flux_array_idx = stream_pos;
}


int parse_flux2(FILE *f, struct track *track, uint8_t header_val, uint32_t stream_pos)
{
	uint8_t val;
	int rc;

	rc = fread(&val, 1, 1, f);
	if (rc < 1) {
		return 1;
	}

	uint16_t fluxval = (header_val << 8) + val;

	append_flux(track, fluxval, stream_pos);

	return 1;
}

int parse_flux3(FILE *f, struct track *track, uint32_t stream_pos)
{
	uint8_t val1, val2;
	int rc;

	rc = fread(&val1, 1, 1, f);
	if (rc < 1) {
		return 1;
	}
	rc = fread(&val2, 1, 1, f);
	if (rc < 1) {
		return 1;
	}

	uint16_t fluxval = (val1 << 8) + val2;

	append_flux(track, fluxval, stream_pos);

	return 1;
}

int parse_kfinfo(FILE *f, struct track *track, uint32_t stream_pos)
{
	uint16_t val;
	char *str;
	int rc;

	rc = fread(&val, 2, 1, f);
	if (rc < 1) {
		return 1;
	}

	str = (char*)malloc(val);
	rc = fread(str, val, 1, f);
	if (rc < 1) {
		return 1;
	}

	rc = sscanf(str, "name=KryoFlux DiskSystem, version=2.20s, date=Jan  8 2015, time=13:49:26, hwid=1, hwrv=1, sck=%lf, ick=%lf", &track->sample_clock, &track->index_clock);

	log_dbg("[%5x] KFINFO: '%s'", stream_pos, str);

	free(str);

	return 1;
}

int parse_oob(FILE *f, struct track *track, uint32_t *stream_pos)
{
	int rc;
	uint8_t val;

	rc = fread(&val, 1, 1, f);
	if (rc < 1) {
		return 1;
	}

	switch (val) {
	case 0x00: {
		uint8_t tmp;
		rc = fread(&tmp, 1, 1, f);
		if (rc < 1 || tmp != 0x00) { return 1; }
		rc = fread(&tmp, 1, 1, f);
		if (rc < 1 || tmp != 0x00) { return 1; }
		rc = fread(&tmp, 1, 1, f);
		if (rc < 1 || tmp != 0x00) { return 1; }

		log_err("Invalid block at pos %x", *stream_pos);
		break;
	}
	case 0x01: {
		uint16_t tmp;
		uint32_t oob_stream_pos;
		uint32_t oob_transfer_time;

		rc = fread(&tmp, 2, 1, f);
		if (rc < 1 || tmp != 0x0008) {
			return 1;
		}

		rc = fread(&oob_stream_pos, 4, 1, f);
		if (rc < 1) {
			return 1;
		}

		rc = fread(&oob_transfer_time, 4, 1, f);
		if (rc < 1) {
			return 1;
		}

		if (*stream_pos != oob_stream_pos) {
			log_dbg("[%5x] Stream Info: pos:%08x; transfer time:%4ums", *stream_pos, oob_stream_pos, oob_transfer_time);
			log_err("WARNING: stream_pos:%06x does not match oob:%06x; resetting",
				*stream_pos, oob_stream_pos);
			*stream_pos = oob_stream_pos;
		}
		else {
			log_dbg("[%5x] Stream Info: transfer time:%4ums", *stream_pos, oob_transfer_time);
		}

		break;
	}
	case 0x02: {
		uint16_t size;
		uint32_t oob_stream_pos;
		uint32_t oob_sample_counter;
		uint32_t oob_index_counter;

		rc = fread(&size, 2, 1, f);
		if (rc < 1 || size != 0x000c) {
			return 1;
		}

		if (size != 0x000c) {
			log_err("Error parsing index block: size %04x", size);
			return 1;
		}

		rc = fread(&oob_stream_pos, 4, 1, f);
		if (rc < 1) {
			return 1;
		}

		rc = fread(&oob_sample_counter, 4, 1, f);
		if (rc < 1) {
			return 1;
		}

		rc = fread(&oob_index_counter, 4, 1, f);
		if (rc < 1) {
			return 1;
		}

		append_index(track, oob_stream_pos, oob_sample_counter, oob_index_counter);

		log_dbg("[%5x] Index block: pos:%08x sample_counter:%08x index_counter:%08x", *stream_pos, oob_stream_pos, oob_sample_counter, oob_index_counter);
		break;
	}
	case 0x03: {
		uint16_t size;
		uint32_t oob_stream_pos;
		uint32_t oob_result_code;

		rc = fread(&size, 2, 1, f);
		if (rc < 1) {
			return 1;
		}

		if (size != 0x0008) {
			log_err("Error parsing index block: size %04x", size);
			return 1;
		}

		rc = fread(&oob_stream_pos, 4, 1, f);
		if (rc < 1) {
			return 1;
		}

		rc = fread(&oob_result_code, 4, 1, f);
		if (rc < 1) {
			return 1;
		}

		log_dbg("[%5x] Stream end: pos:%08x result_code:%08x", *stream_pos, oob_stream_pos, oob_result_code);
		break;
	}
	case 0x04: {
		parse_kfinfo(f, track, *stream_pos);
		break;
	}
	case 0x0d: {
		uint8_t tmp;
		rc = fread(&tmp, 1, 1, f);
		if (rc < 1 || tmp != 0x0d) { return 1; }
		rc = fread(&tmp, 1, 1, f);
		if (rc < 1 || tmp != 0x0d) { return 1; }
		rc = fread(&tmp, 1, 1, f);
		if (rc < 1 || tmp != 0x0d) { return 1; }

		log_dbg("[%5x] EOF", *stream_pos);
		break;
	}
	default: {
		log_err("Unknown OOB type %x", val);
		break;
	}
	}

	return 0;
}

int parse_flux_stream(char *fn, struct track *track, uint8_t side, uint8_t track_num)
{
	FILE *input;
	uint32_t stream_pos = 0;

	input = fopen(fn, "r");
	if (input == NULL) {
		return 1;
	}

	track->master_clock = ((18432000 * 73) / 14.0) / 2.0;
	track->sample_clock = track->master_clock / 2;
	track->index_clock  = track->master_clock / 16;

	track->side  = side;
	track->track = track_num;

	track->indices_idx = 0;
	track->indices_max = 1;
	track->indices = (struct index *)malloc(sizeof(struct index)*track->indices_max);

	track->flux_array_idx = 0;
	track->flux_array_max = 1;
	track->flux_array = NULL;

	LIST_INIT(&(track->sectors));

	log_dbg("CLOCKS: %.10f %.10f %.10f",
		track->master_clock, track->sample_clock, track->index_clock);


	uint8_t val;
	int rc;
	int pos = 0;

	while (1) {
		rc = fread(&val, 1, 1, input);
		if (rc < 1) {
			break;
		}

		switch (val) {
		case 0x00:
		case 0x01:
		case 0x02:
		case 0x03:
		case 0x04:
		case 0x05:
		case 0x06:
		case 0x07: {
			parse_flux2(input, track, val, stream_pos);
			stream_pos += 2;
			break;
		}
		// one-byte no-op
		case 0x08: {
			stream_pos += 1;
			// no-op
			break;
		}
		// two-byte no-op
		case 0x09: {
			stream_pos += 2;
			rc = fread(&val, 1, 1, input);
			if (rc < 1) { log_err("Failed read at pos %u", pos); break; }
			break;
		}
		// three-byte no-op
		case 0x0a: {
			stream_pos += 3;
			rc = fread(&val, 1, 1, input);
			if (rc < 1) { log_err("Failed read at pos %u", pos); break; }
			rc = fread(&val, 1, 1, input);
			if (rc < 1) { log_err("Failed read at pos %u", pos); break; }
			break;
		}
		case 0x0b: {
			log_dbg("ALERT: next flux block should be += 0x10000");
			stream_pos += 1;
			break;
		}
		case 0x0c: {
			parse_flux3(input, track, stream_pos);
			stream_pos += 3;
			break;
		}
		case 0x0d: {
			parse_oob(input, track, &stream_pos);
			break;
		}
		default: {
			if (val >= 0x0e) {
				append_flux(track, val, stream_pos);
			}
			else {
				log_err("Error: Unknown block type %x", val);
			}
			stream_pos += 1;
		}
		}
	}

	fclose(input);

	return 0;
}

void dump_stream(struct track *track)
{
	uint32_t i;
	for (i = 0; i < track->flux_array_idx; i++) {
		log_dbg("FLUX:  stream_pos:%8x flux_val:%8x", i, track->flux_array[i]);
	}
	for (i = 0; i < track->indices_idx; i++) {
		log_dbg("INDEX: stream_pos:%8x sample_count:%8x index_counter:%8x",
			i,
			track->indices[i].sample_counter,
			track->indices[i].index_counter);
	}
}

int test_flux_timing(double flux_us)
{
	if (	(flux_us > 0.0000035 && flux_us < 0.0000045) ||
		(flux_us > 0.0000055 && flux_us < 0.0000065) ||
		(flux_us > 0.0000075 && flux_us < 0.0000085) ) {
		return 0;
	}

	return 1;
}

int decode_pass(struct track *track, uint32_t index, uint32_t next_index, uint32_t pass, uint32_t *flux_sum)
{
	uint32_t flux_count = 0;

	if (index >= track->flux_array_idx) {
		log_err("[S:%x, T:%02u, PASS:%u] WARNING: SEEK ERROR ON STREAM_POS %x", track->side, track->track, pass, index);
		return index;
	}

	// parse whole track
	int error_count = 0;
	while (index < next_index && index < track->flux_array_idx) {
		double flux_us = track->flux_array[index] / track->sample_clock;
		if (test_flux_timing(flux_us)) {
			error_count++;
		}

		*flux_sum += track->flux_array[index];
		flux_count++;
		index++;

		/* double density is MFM encoding
		 * That's basically:
		 * 00: reversal    + no reversal
		 * 01: no reversal + no reversal
		 * 1:  no reversal + reversal
		 * minimum measurable gap: ~0.2us since last reversal? between reversals?
		 * "elapsed time between two flux reversals, or between a Flux reversal and an Index Signal."
		 * valid combos:
		 * - 
		*/
	}

	// Decoder must manually insert an empty flux at the end.
	if (index != next_index) {
		log_err("[Phase 1: S:%x, T:%02u, PASS:%u, next_index:%5x] NOT FOUND, AT END? %x %x",
			track->side, track->track, pass, next_index, index-1, next_index);
	}

	if (pass < track->stats.pass_count_max) {
		track->stats.error_rate[pass] = (error_count / (float)flux_count * 100);
	}

	return index;
}

int decode_flux(struct track *track)
{
	uint32_t pass;

	uint32_t last_index_counter  = 0;
	uint32_t last_sample_counter = 0;

	track->stats.pass_count_max = PASS_COUNT_DEFAULT;
	track->stats.error_rate     = (double *)malloc(sizeof(double)*PASS_COUNT_DEFAULT);

	for (pass = 0; pass < track->indices_idx; pass++) {
		log_dbg("[S:%u, T:%02u, PASS:%x] INDEX: %05x %0.3f [%0.3f:%0.3f:%0.3f] %x",
			track->side, track->track, pass,
			track->indices[pass].stream_pos,
			track->indices[pass].sample_counter / track->sample_clock * 1000 * 1000,
			track->flux_array[track->indices[pass].stream_pos-1] / track->sample_clock * 1000 * 1000,
			track->flux_array[track->indices[pass].stream_pos]  / track->sample_clock * 1000 * 1000,
			track->flux_array[track->indices[pass].stream_pos+1]  / track->sample_clock * 1000 * 1000,
			track->indices[pass].index_counter);
	}

	/* Method to calculate the time between two indices:
	 * page 10: It can also be calculated by summing all the flux reversal
	 * values that we recorded since the previous index, adding the Sample
	 * Counter value at which the index was detected (see Sample Counter
	 * field in Index Block) and subtracting the Sample Counter value of
	 * the previous index.
	 */
	pass = 0;
	while (track->indices_idx && pass < (track->indices_idx - 1)) {
		uint32_t flux_sum       = 0;
		uint32_t index_pos      = track->indices[pass].stream_pos;
		uint32_t next_index_pos = track->indices[pass+1].stream_pos;


		decode_pass(track, index_pos, next_index_pos, pass, &flux_sum);

		log_dbg("[Phase 1: S:%x, T:%02u, PASS:%x] SAMPLE CLOCK: %0.3fus",
			track->side, track->track, pass,
			track->indices[pass].sample_counter / track->sample_clock * 1000 * 1000);

		log_dbg("[Phase 1: S:%x, T:%02u, PASS:%x] INDEX CLOCK:  %f (%f)",
			track->side, track->track, pass,
			track->indices[pass].index_counter/track->index_clock,
			pass ? (track->indices[pass].index_counter - last_index_counter)/track->index_clock : 0.0);

		uint32_t diff = flux_sum - last_sample_counter + track->indices[pass].sample_counter;
		log_dbg("[Phase 1: S:%x, T:%02u, PASS:%u] Space between indices: %0.3fms; %0.3f RPM",
			track->side, track->track, pass,
			diff/track->sample_clock * 1000,
			60/(diff/track->sample_clock));

		last_index_counter  = track->indices[pass].index_counter;
		last_sample_counter = track->indices[pass].sample_counter;

		pass++;
	}

	uint16_t i = 0;
	double total = 0;
	for ( ; i < pass; i++) {
		total += track->stats.error_rate[i];
	}
	log_msg("[Phase 1: S:%x, T:%02u] %f average error rate", track->side, track->track, total);

	return 0;
}

void free_stream(struct track *track)
{
	free(track->flux_array);
	track->flux_array = NULL;
	track->flux_array_idx = 0;
	track->flux_array_max = 1;

	bytestream_destroy(&(track->stream));
	free(track->stats.error_rate);

	free(track->indices);
	track->indices = NULL;
	track->indices_idx = 0;
	track->indices_max = 1;
}

