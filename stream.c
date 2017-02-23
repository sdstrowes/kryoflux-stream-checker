#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stream.h"


uint32_t stream_pos;


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
	if (track->flux_array_idx >= track->flux_array_max - 1) {
		uint32_t old_max = track->flux_array_max;
		track->flux_array_max *= 2;
//		struct flux *tmp = (struct flux *)realloc(track->flux_array, sizeof(struct flux)*track->flux_array_max);
//		track->flux_array = tmp;


		struct flux *tmp = (struct flux *)calloc(track->flux_array_max, sizeof(struct flux));
		if (track->flux_array != NULL) {
			memcpy(tmp, track->flux_array, old_max*sizeof(struct flux));
		}
		track->flux_array = tmp;
	}

	track->flux_array[track->flux_array_idx].flux_val   = flux_val;
	track->flux_array[track->flux_array_idx].stream_pos = stream_pos;
	track->flux_array_idx++;
}


int parse_flux2(FILE *f, struct track *track, uint8_t header_val)
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

int parse_flux3(FILE *f, struct track *track)
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

int parse_kfinfo(FILE *f, struct track *track)
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

	printf("[%5x] KFINFO: '%s'\n", stream_pos, str);
	//stream_pos += 4 + val;

	free(str);

	return 1;
}

int parse_oob(FILE *f, struct track *track)
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

		fprintf(stderr, "Invalid block at pos %x\n", stream_pos);
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

		if (stream_pos != oob_stream_pos) {
			printf("[%5x] Stream Info: pos:%08x; transfer time:%4ums\n", stream_pos, oob_stream_pos, oob_transfer_time);
			printf("WARNING: stream_pos:%06x does not match oob:%06x; resetting\n",
				stream_pos, oob_stream_pos);
			stream_pos = oob_stream_pos;
		}
		else {
			printf("[%5x] Stream Info: transfer time:%4ums\n", stream_pos, oob_transfer_time);
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
			fprintf(stderr, "Error parsing index block: size %04x\n", size);
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

		printf("[%5x] Index block: pos:%08x sample_counter:%08x index_counter:%08x\n", stream_pos, oob_stream_pos, oob_sample_counter, oob_index_counter);
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
			fprintf(stderr, "Error parsing index block: size %04x\n", size);
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

		printf("[%5x] Stream end: pos:%08x result_code:%08x\n", stream_pos, oob_stream_pos, oob_result_code);
		//stream_pos += 12;
		break;
	}
	case 0x04: {
		parse_kfinfo(f, track);
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

		printf("[%5x] EOF\n", stream_pos);
		//stream_pos += 4;
		break;
	}
	default: {
		fprintf(stderr, "ruh roh\n");
		break;
	}
	}

	return 0;
}

int parse_stream(char *fn, struct track *track, uint8_t side, uint8_t track_num)
{
	FILE *input;

	input = fopen(fn, "r");
	if (input == NULL) {
		fprintf(stderr, "Can't open file %s\n", fn);
		return 1;
	}

	track->master_clock = ((18432000 * 73) / 14.0) / 2.0;
	track->sample_clock = track->master_clock / 2;
	track->index_clock  = track->master_clock / 16;

	stream_pos = 0;

	track->side  = side;
	track->track = track_num;

	track->indices_idx = 0;
	track->indices_max = 1;
	track->indices = (struct index *)malloc(sizeof(struct index)*track->indices_max);

	track->flux_array_idx = 0;
	track->flux_array_max = 1;
	track->flux_array = NULL;
//(struct flux *)calloc(sizeof(struct flux)*track->flux_array_max);


	printf("CLOCKS: %.10f %.10f %.10f\n",
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
			parse_flux2(input, track, val);
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
			if (rc < 1) { fprintf(stderr, "Failed read at pos %u\n", pos); break; }
			break;
		}
		// three-byte no-op
		case 0x0a: {
			stream_pos += 3;
			rc = fread(&val, 1, 1, input);
			if (rc < 1) { fprintf(stderr, "Failed read at pos %u\n", pos); break; }
			rc = fread(&val, 1, 1, input);
			if (rc < 1) { fprintf(stderr, "Failed read at pos %u\n", pos); break; }
			break;
		}
		case 0x0b: {
			printf("ALERT: next flux block should be += 0x10000\n");
			stream_pos += 1;
			break;
		}
		case 0x0c: {
			parse_flux3(input, track);
			stream_pos += 3;
			break;
		}
		case 0x0d: {
			parse_oob(input, track);
			break;
		}
		default: {
			if (val >= 0x0e) {
				append_flux(track, val, stream_pos);
			}
			else {
				printf("ERROR: Unknown block type %x\n", val);
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
		printf("FLUX:  stream_pos:%8x flux_val:%8x\n",
			track->flux_array[i].stream_pos,
			track->flux_array[i].flux_val);
	}
	for (i = 0; i < track->indices_idx; i++) {
		printf("INDEX: stream_pos:%8x sample_count:%8x index_counter:%8x\n",
			track->indices[i].stream_pos,
			track->indices[i].sample_counter,
			track->indices[i].index_counter);
	}
}

int decode_track(struct track *track, uint32_t index, uint32_t next_index, uint32_t i, uint32_t j, uint32_t *flux_sum)
{
	uint32_t flux_count = 0;

	// Seek forward
	while (j < track->flux_array_idx && track->flux_array[j].stream_pos < index) {
		j++;
	}

	if (j < track->flux_array_idx && track->flux_array[j].stream_pos != index) {
		printf("WARNING: SEEK ERROR ON INDEX %u, %x", i, index);
		return j;
	}

	// parse whole track
	while (j < track->flux_array_idx && track->flux_array[j].stream_pos < next_index) {
		printf("Flux: %u %u %u %u %u %0.8f\n", 
			track->side, track->track,
			i, track->flux_array[j].stream_pos - index,
			track->flux_array[j].flux_val,
			track->flux_array[j].flux_val/track->sample_clock);
		*flux_sum   += track->flux_array[j].flux_val;
		flux_count += 1;
		j++;
	}

	// Decoder must manually insert an empty flux at the end.
	if (track->flux_array[j].stream_pos != next_index) {
		printf("[%5x] NOT FOUND, AT END? %x %x\n",
			next_index,
			track->flux_array[j-1].stream_pos, next_index);
	}

	printf("SUMMED? %u\n", *flux_sum);



	printf("\n");

	return j;
}

int decode_stream(struct track *track)
{
	uint32_t i = 0;
	uint32_t j = 0;

	uint32_t last_index_counter  = 0;
	uint32_t last_sample_counter = 0;

	printf("--- Decoding ---\n");

	while (track->indices_idx && i < (track->indices_idx - 1)) {
		uint32_t flux_sum       = 0;
		uint32_t index_pos      = track->indices[i].stream_pos;
		uint32_t next_index_pos = track->indices[i+1].stream_pos;

		j = decode_track(track, index_pos, next_index_pos, i, j, &flux_sum);

		// INDEX TIME is the number of clock cycles since the last
		// index occurred

		// flux val
		// stream pos
		// index->stream_pos: next flux transition
		// index->sample_counter: how far from beginning of prev flux trans
		// index clock

		printf("[%5x] IDX:%x SAMPLE CLOCK: %04x (%f)\n",
			index_pos,
			i,
			track->indices[i].sample_counter,
			track->indices[i].sample_counter / track->sample_clock);

		printf("[%5x] IDX:%x INDEX CLOCK:  %f (%f)\n",
			index_pos,
			i,
			track->indices[i].index_counter/track->index_clock,
			i ? (track->indices[i].index_counter - last_index_counter)/track->index_clock : 0.0);
		uint32_t diff = flux_sum - last_sample_counter + track->indices[i].sample_counter;
		printf("CALCED: %u - %u + %u == %u (%f)\n",
			flux_sum, last_sample_counter, track->indices[i].sample_counter,
			diff,
			diff/track->sample_clock);
		printf("RPM: %f\n", 60/(diff/track->sample_clock));
//		if (i != 0) {
//			printf("COUNT: %u %u %u %u\n", track->side, track->track, i, flux_count);
//		}

		last_index_counter  = track->indices[i].index_counter;
		last_sample_counter = track->indices[i].sample_counter;

		i++;
	}

	return 0;
}

int vis_stream(struct track *track)
{
//	uint32_t flux_count = 0;

	uint32_t pos = 0;

	uint32_t start = track->indices[0].stream_pos;
	uint32_t end   = track->indices[1].stream_pos;

	// Seek forward to start
	uint32_t j = 0;
	while (j < track->flux_array_idx && pos < start) {
		pos++;
	}

	printf("parp: %x %x\n", start, end);

//	if (j < track->flux_array_idx && track->flux_array[j].stream_pos != index) {
//		printf("WARNING: SEEK ERROR ON INDEX %u, %x", i, index);
//		return j;
//	}

	// parse whole track
	double fraction = (360.0/(end-start));
	while (pos < track->flux_array_idx && pos < end) {
		printf("pos: %u end:%u start:%u fraction:%f angle:%f x:%f y:%f\n",
			pos-start, end, start, fraction, (pos-start)*fraction,
			track->track * sin((pos-start)*fraction*(M_PI/180)),
			track->track * cos((pos-start)*fraction*(M_PI/180)));
		pos++;
	}
//
//	// Decoder must manually insert an empty flux at the end.
//	if (track->flux_array[j].stream_pos != next_index) {
//		printf("[%5x] NOT FOUND, AT END? %x %x\n",
//			next_index,
//			track->flux_array[j-1].stream_pos, next_index);
//	}

	return 0;
}

void plot_track(struct track *track, struct colour *buffer, uint16_t width, uint16_t height)
{
	uint32_t pos = 0;

	uint32_t start = track->indices[0].stream_pos;
	uint32_t end   = track->indices[1].stream_pos;

	// Seek forward to start
	uint32_t j = 0;
	while (j < track->flux_array_idx && pos < start) {
		pos++;
	}

	printf("parp: %x %x\n", start, end);

//	if (j < track->flux_array_idx && track->flux_array[j].stream_pos != index) {
//		printf("WARNING: SEEK ERROR ON INDEX %u, %x", i, index);
//		return j;
//	}

	// parse whole track
	double fraction = (360.0/(end-start));
	while (pos < track->flux_array_idx && pos < end) {
		float x = track->track * 0.5 * sin((pos-start)*fraction*(M_PI/180));
		float y = track->track * 0.5 * cos((pos-start)*fraction*(M_PI/180));
		uint32_t x_scaled = x*6 + (width/2);
		uint32_t y_scaled = y*6 + (height/2);

		printf("pos: %u end:%u start:%u fraction:%f angle:%f x:%f y:%f, x:%u y:%u, fx:%u\n",
			pos-start, end, start, fraction, (pos-start)*fraction,
			x, y, x_scaled, y_scaled, track->flux_array[pos].flux_val);

		if (track->flux_array[pos].flux_val/track->sample_clock > 0.0000075) {
			buffer[y_scaled*width + x_scaled].r = 255;
		}
		if (track->flux_array[pos].flux_val/track->sample_clock > 0.0000055) {
			buffer[y_scaled*width + x_scaled].g = 255;
		}
		if (track->flux_array[pos].flux_val/track->sample_clock > 0.0000035) {
			buffer[y_scaled*width + x_scaled].b = 255;
		}
		

		//buffer[y_scaled*width + x_scaled] = (float)track->flux_array[pos].flux_val;
		pos++;
	}

}

void free_stream(struct track *track)
{
	free(track->flux_array);
	track->flux_array = NULL;
	track->flux_array_idx = 0;
	track->flux_array_max = 1;

	free(track->indices);
	track->indices = NULL;
	track->indices_idx = 0;
	track->indices_max = 1;
}

