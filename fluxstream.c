#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kf-info.h"
#include "kf-oob.h"
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



void append_stream(struct track *track, flux_t flux_val, uint32_t stream_pos)
{
	if (stream_pos >= track->stream_buf_max - 1) {
		uint32_t old_max = track->stream_buf_max;
		track->stream_buf_max *= 2;

		flux_t *tmp = (flux_t *)calloc(track->stream_buf_max, sizeof(flux_t));
		if (track->stream_buf != NULL) {
			memcpy(tmp, track->stream_buf, old_max*sizeof(flux_t));
			free(track->stream_buf);
		}
		track->stream_buf = tmp;
	}

	track->stream_buf[stream_pos] = flux_val;

	// idx becomes a marker for the last entry in the array
	track->stream_buf_idx = stream_pos;
}


int parse_flux2(FILE *f, struct track *track, uint8_t header_val, bool ovl16, uint32_t stream_pos)
{
	uint8_t val;
	int rc;

	rc = fread(&val, 1, 1, f);
	if (rc < 1) {
		log_err("fread() fail");
		exit(1);
	}

	flux_t fluxval = (header_val << 8) + val;
	if (ovl16) {
		fluxval = 0x10000 + fluxval;
	}

	log_dbg("flux2: header: %02x value:%02x", header_val, val);
	log_dbg("flux2: appending %08x (pos %04x)", fluxval, stream_pos);

	append_stream(track, fluxval, stream_pos);

	return 1;
}

int parse_flux3(FILE *f, struct track *track, bool ovl16, uint32_t stream_pos)
{
	uint8_t val1, val2;
	int rc;

	rc = fread(&val1, 1, 1, f);
	if (rc < 1) {
		log_err("fread() fail");
		exit(1);
	}
	rc = fread(&val2, 1, 1, f);
	if (rc < 1) {
		log_err("fread() fail");
		exit(1);
	}
	flux_t fluxval = (val1 << 8) + val2;
	if (ovl16) {
		fluxval = 0x10000 + fluxval;
	}

	append_stream(track, fluxval, stream_pos);

	return 1;
}


int parse_flux_stream(char *fn, struct track *track, uint8_t side, uint8_t track_num)
{
	FILE *input;
	uint32_t stream_pos = 0;

	input = fopen(fn, "r");
	if (input == NULL) {
		return 1;
	}

	// values borrowed from http://www.softpres.org/kryoflux:stream
	track->master_clock = ((18432000 * 73) / 14.0) / 2.0;
	track->sample_clock = track->master_clock / 2;
	track->index_clock  = track->master_clock / 16;

	track->side  = side;
	track->track = track_num;

	track->indices_idx = 0;
	track->indices_max = 1;
	track->indices = (struct index *)malloc(sizeof(struct index)*track->indices_max);

	track->stream_buf_idx = 0;
	track->stream_buf_max = 1;
	track->stream_buf = NULL;

	LIST_INIT(&(track->sectors));

	log_dbg("CLOCKS: %.10f %.10f %.10f",
		track->master_clock, track->sample_clock, track->index_clock);


	uint8_t encoding_marker;
	int rc;
	bool eod = false;
	bool ovl16 = false;

	while (!eod) {
		rc = fread(&encoding_marker, 1, 1, input);
		if (rc < 1) {
			break;
		}


		// http://www.softpres.org/kryoflux:stream
		switch (encoding_marker) {
		case 0x00:
		case 0x01:
		case 0x02:
		case 0x03:
		case 0x04:
		case 0x05:
		case 0x06:
		case 0x07: {
			log_dbg("SECTION [%02x] flux2", encoding_marker);
			parse_flux2(input, track, encoding_marker, ovl16, stream_pos);
			ovl16 = false; // if this was set, clear it.
			stream_pos += 2;
			break;
		}
		// one-byte no-op
		case 0x08: {
			log_dbg("SECTION [%02x] no-op 1", encoding_marker);
			stream_pos += 1;
			// no-op
			break;
		}
		// two-byte no-op
		case 0x09: {
			log_dbg("SECTION [%02x] no-op 2", encoding_marker);
			rc = fseek(input, 1, SEEK_CUR);
			if (rc != 0) {
				log_err("fseek() failed at pos %u: \"%s\"", stream_pos, strerror(errno));
				exit(1);
			}
			stream_pos += 2;
			break;
		}
		// three-byte no-op; seek forward two additional bytes
		case 0x0a: {
			log_dbg("SECTION [%02x] no-op 3", encoding_marker);
			rc = fseek(input, 2, SEEK_CUR);
			if (rc != 0) {
				log_err("fseek() failed at pos %u: \"%s\"", stream_pos, strerror(errno));
				exit(1);
			}
			stream_pos += 3;
			break;
		}
		case 0x0b: {
			log_dbg("SECTION [%02x] Overflow16, next flux block should be += 0x10000?", encoding_marker);
			log_err("ALERT: next flux block should be += 0x10000");
			stream_pos += 1;
			break;
		}
		case 0x0c: {
			log_dbg("SECTION [%02x] flux3", encoding_marker);
			parse_flux3(input, track, ovl16, stream_pos);
			ovl16 = false; // if this was set, clear it.
			stream_pos += 3;
			break;
		}
		case 0x0d: {
			rc = parse_oob(input, track, &stream_pos);
			if (rc == 1) {
				eod = true;
			}
			else if (rc >= 2) {
				log_err("Error parsing OOB block");
				exit(1);
			}
			break;
		}
		default: {
			if (encoding_marker >= 0x0e) {
				if (ovl16) {
					append_stream(track, 0x10000 + encoding_marker, stream_pos);
					ovl16 = false;
				}
				else {
					append_stream(track, encoding_marker, stream_pos);
				}
			}
			else {
				log_err("Error: Unknown block type %x", encoding_marker);
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
	for (i = 0; i < track->stream_buf_idx; i++) {
		log_dbg("FLUX:  stream_pos:%8x flux_val:%8x", i, track->stream_buf[i]);
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

	if (index >= track->stream_buf_idx) {
		log_err("[S:%x, T:%02u, PASS:%u] WARNING: SEEK ERROR ON STREAM_POS %x", track->side, track->track, pass, index);
		return index;
	}

	// parse whole track
	int error_count = 0;
	while (index < next_index && index < track->stream_buf_idx) {
		double flux_us = track->stream_buf[index] / track->sample_clock;
		if (test_flux_timing(flux_us)) {
			error_count++;
		}

		*flux_sum += track->stream_buf[index];
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
			track->stream_buf[track->indices[pass].stream_pos-1] / track->sample_clock * 1000 * 1000,
			track->stream_buf[track->indices[pass].stream_pos]  / track->sample_clock * 1000 * 1000,
			track->stream_buf[track->indices[pass].stream_pos+1]  / track->sample_clock * 1000 * 1000,
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
	free(track->stream_buf);
	track->stream_buf = NULL;
	track->stream_buf_idx = 0;
	track->stream_buf_max = 1;

	bytestream_destroy(&(track->stream));
	free(track->stats.error_rate);

	free(track->indices);
	track->indices = NULL;
	track->indices_idx = 0;
	track->indices_max = 1;
}

