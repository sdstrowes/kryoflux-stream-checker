#include <errno.h>
#include <stdbool.h>
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
	if (sector == NULL) {
		log_err("malloc failed in sector_init");
		exit(1);
	}
	sector->pass_count    = 0;
	memset(&sector->meta, 0, sizeof(struct sector_meta));
	memset(&sector->data, 0, sizeof(struct sector_pass));
	sector->meta.revolution = UINT32_MAX;
	sector->data.data     = NULL;
	sector->data.data_len = 0;

	*s = sector;
}


uint32_t append_stream(struct track *track, flux_t flux_val, uint32_t flux_buffer_pos)
{
	if (flux_buffer_pos >= track->flux_buf_max - 1) {
		uint32_t old_max = track->flux_buf_max;
		track->flux_buf_max *= 2;

		struct flux_entry *tmp = (struct flux_entry *)calloc(track->flux_buf_max, sizeof(struct flux_entry));
		if (tmp == NULL) {
			log_err("calloc failed in append_stream");
			exit(1);
		}
		if (track->flux_buffer != NULL) {
			memcpy(tmp, track->flux_buffer, old_max*sizeof(struct flux_entry));
			free(track->flux_buffer);
		}
		track->flux_buffer = tmp;
	}

	track->flux_buffer[flux_buffer_pos].val = flux_val;

	// idx becomes a marker for the last entry in the array
	track->flux_buf_idx = flux_buffer_pos;

	//*flux_buffer_pos = *flux_buffer_pos + 1;

	return flux_buffer_pos + 1;
}


int parse_flux2(struct track *track, uint8_t header_val, bool ovl16, uint32_t stream_pos, uint32_t flux_buffer_pos)
{
	uint8_t val = track->isb[stream_pos];

	flux_t fluxval = (header_val << 8) + val;
	if (ovl16) {
		fluxval = 0x10000 + fluxval;
	}

	log_dbg("flux2: header: %02x value:%02x", header_val, val);
	log_dbg("flux2: appending %08x (pos %04x)", fluxval, stream_pos);

	flux_buffer_pos = append_stream(track, fluxval, flux_buffer_pos);

	return flux_buffer_pos;
}

int parse_flux3(struct track *track, bool ovl16, uint32_t stream_pos, uint32_t flux_buffer_pos)
{
	uint8_t val1 = track->isb[stream_pos];
	uint8_t val2 = track->isb[stream_pos+1];

	flux_t fluxval = (val1 << 8) + val2;
	if (ovl16) {
		fluxval = 0x10000 + fluxval;
	}

	flux_buffer_pos = append_stream(track, fluxval, flux_buffer_pos);

	return flux_buffer_pos;
}



void dump_stream(struct track *track)
{
	uint32_t i;
	for (i = 0; i < track->flux_buf_idx; i++) {
		log_dbg("FLUX:  stream_pos:%8x flux_val:%8x", i, track->flux_buffer[i].val);
	}
	for (i = 0; i < track->indices_idx; i++) {
		log_dbg("INDEX: stream_pos:%8x sample_count:%8x index_counter:%8x",
			i,
			track->index[i].sample_counter,
			track->index[i].index_counter);
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

	if (index >= track->flux_buf_idx) {
		log_err("[S:%x, T:%02u, PASS:%u] WARNING: SEEK ERROR ON STREAM_POS %x", track->side, track->track, pass, index);
		return index;
	}

	// parse whole track
	int error_count = 0;
	while (index < next_index && index < track->flux_buf_idx) {
		double flux_us = track->flux_buffer[index].val / track->sample_clock;
		if (test_flux_timing(flux_us)) {
			error_count++;
		}

		*flux_sum += track->flux_buffer[index].val;
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

	track->stats.pass_count_max = PASS_COUNT_DEFAULT;
	track->stats.error_rate     = (double *)malloc(sizeof(double)*PASS_COUNT_DEFAULT);

	for (pass = 0; pass < track->indices_idx; pass++) {
		log_dbg("[S:%u, T:%02u, PASS:%x] INDEX: %05x %0.3f [%0.3f:%0.3f:%0.3f] %x",
			track->side, track->track, pass,
			track->index[pass].stream_pos,
			track->index[pass].sample_counter / track->sample_clock * 1000 * 1000,
			track->flux_buffer[track->index[pass].stream_pos-1].val / track->sample_clock * 1000 * 1000,
			track->flux_buffer[track->index[pass].stream_pos].val  / track->sample_clock * 1000 * 1000,
			track->flux_buffer[track->index[pass].stream_pos+1].val  / track->sample_clock * 1000 * 1000,
			track->index[pass].index_counter);
	}

	/* Method to calculate the time between two indices:
	 * page 10: It can also be calculated by summing all the flux reversal
	 * values that we recorded since the previous index, adding the Sample
	 * Counter value at which the index was detected (see Sample Counter
	 * field in Index Block) and subtracting the Sample Counter value of
	 * the previous index.
	 */
	for (pass = 0 ; pass < track->indices_idx - 1; pass++) {
	//while (track->indices_idx && pass < (track->indices_idx - 1)) {
		uint32_t flux_sum       = 0;
		uint32_t index_pos      = track->index[pass].stream_pos;
		uint32_t next_index_pos = track->index[pass+1].stream_pos;


		decode_pass(track, index_pos, next_index_pos, pass, &flux_sum);

		log_dbg("[Phase 1: S:%x, T:%02u, PASS:%x] SAMPLE CLOCK: %0.3fus",
			track->side, track->track, pass,
			track->index[pass].sample_counter / track->sample_clock * 1000 * 1000);

		log_dbg("[Phase 1: S:%x, T:%02u, PASS:%x] INDEX CLOCK:  %f (%f)",
			track->side, track->track, pass,
			track->index[pass].index_counter/track->index_clock,
			(track->index[pass+1].index_counter - track->index[pass].index_counter)/track->index_clock);

		uint32_t diff = flux_sum + track->index[pass+1].sample_counter - track->index[pass].sample_counter;
		log_dbg("[Phase 1: S:%x, T:%02u, PASS:%u] Space between indices: %0.3fms; %0.3f RPM",
			track->side, track->track, pass,
			diff/track->sample_clock * 1000,
			60/(diff/track->sample_clock));
	}

	uint16_t i = 0;
	double total = 0;
	for ( ; i < pass; i++) {
		total += track->stats.error_rate[i];
	}
	log_msg("[Phase 1: S:%x, T:%02u] %f average error rate", track->side, track->track, total);

	return 0;
}


//int parse_flux_stream(&disk->side[side].t[track], uint8_t side, uint8_t track)
//int parse_flux_stream(struct track *track, uint8_t side, uint8_t track_num)
//{
//	//int i = 0;
//	bool ovl16 = false;
//	int stream_pos = 0;
//	int flux_buffer_pos = 0;
//	while (stream_pos < track->isb_idx) {
//
//		// http://www.softpres.org/kryoflux:stream
//		uint8_t encoding_marker = track->isb[stream_pos];
//		switch (encoding_marker) {
//		case 0x00:
//		case 0x01:
//		case 0x02:
//		case 0x03:
//		case 0x04:
//		case 0x05:
//		case 0x06:
//		case 0x07: {
//			log_dbg("SECTION [%02x] flux2", encoding_marker);
//			flux_buffer_pos = parse_flux2(track, encoding_marker, ovl16, stream_pos, flux_buffer_pos);
//			ovl16 = false;
//			stream_pos += 2;
//			break;
//		}
//		// one-byte no-op
//		case 0x08: {
//			log_dbg("SECTION [%02x] no-op 1", encoding_marker);
//			stream_pos += 1;
//			break;
//		}
//		// two-byte no-op
//		case 0x09: {
//			log_dbg("SECTION [%02x] no-op 2", encoding_marker);
//			stream_pos += 2;
//			break;
//		}
//		// three-byte no-op; seek forward two additional bytes
//		case 0x0a: {
//			log_dbg("SECTION [%02x] no-op 3", encoding_marker);
//			stream_pos += 3;
//			break;
//		}
//		// ovl16 ("overflow")
//		case 0x0b: {
//			log_dbg("SECTION [%02x] Overflow16, next flux block should be += 0x10000?", encoding_marker);
//			ovl16 = true;
//			stream_pos += 1;
//			break;
//		}
//		// flux3
//		case 0x0c: {
//			log_dbg("SECTION [%02x] flux3", encoding_marker);
//			flux_buffer_pos = parse_flux3(track, ovl16, stream_pos, flux_buffer_pos);
//			ovl16 = false;
//
//			stream_pos += 3;
//			break;
//		}
//		case 0x0d: {
//			log_err("OOB found in ISB stream");
//		}
//		default: {
//			if (encoding_marker >= 0x0e) {
//				if (ovl16) {
//					flux_buffer_pos = append_stream(track, 0x10000 + encoding_marker, flux_buffer_pos);
//					ovl16 = false;
//				}
//				else {
//					flux_buffer_pos = append_stream(track, encoding_marker, flux_buffer_pos);
//				}
//			}
//			else {
//				log_err("Error: Unknown block type %x", encoding_marker);
//			}
//			stream_pos += 1;
//		}
//		}
//	}
//
//	return 0;
//
//}


void free_stream(struct track *track)
{
	free(track->flux_buffer);
	track->flux_buffer = NULL;
	track->flux_buf_idx = 0;
	track->flux_buf_max = 1;

	bytestream_destroy(&(track->stream));
	free(track->stats.error_rate);

	free(track->index);
	track->index = NULL;
	track->indices_idx = 0;
	track->indices_max = 1;
}

