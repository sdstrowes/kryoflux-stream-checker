#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "disk-analysis-log.h"
#include "mfm.h"

const uint16_t SECTOR_LEN[] = { 128, 256, 512, 1024 };

enum {UNSYNCED, SEEKING_PRE_ID, FOUND_ID, SEEKING_POST_ID, SEEKING_DATA, FOUND_DATA, SEEKING_POST_DATA, TRACK_COMPLETE} parser_state;


void bytestream_init(struct bytestream **s)
{
	struct bytestream *stream = (struct bytestream *)malloc(sizeof(struct bytestream));

	memset(stream->stream,   0, STREAM_BUFFER_SIZE  );
	memset(stream->time_idx, 0, STREAM_BUFFER_SIZE  );
	memset(stream->recent,   0, STREAM_RECENT_WINDOW);
	stream->ptr = 0;
	stream->subptr = 0;

	*s = stream;
}

void bytestream_destroy(struct bytestream **s)
{
	struct bytestream *stream = *s;
	free(stream);
	*s = NULL;
}

void print_d_c_vals(uint8_t val, uint8_t side, uint8_t track, uint32_t idx)
{
	// FIXME
	side = side; track = track; idx = idx;

	uint8_t part1 = 0;
	uint8_t part2 = 0;

	part1 =  (val & 0x01)       | ((val & 0x04) >> 1) | ((val & 0x10) >> 2) | ((val & 0x40) >> 3);
	part2 = ((val & 0x02) >> 1) | ((val & 0x08) >> 2) | ((val & 0x20) >> 3) | ((val & 0x80) >> 4);

	char buffer1[5], buffer2[5], buffer3[9];
	buffer1[4] = '\0';
	buffer2[4] = '\0';
	buffer3[8] = '\0';
	print_bin(buffer1, part1, 4);
	print_bin(buffer2, part2, 4);
	print_bin(buffer3,   val, 8);
}

void bytestream_get_timer(struct bytestream *stream, int location, double *timer)
{
	int ptr = location / 8;
	*timer = stream->time_idx[ptr];
}

void bytestream_get_location(struct bytestream *stream, int location, uint8_t *buffer, int length)
{
	int ptr = location / 8;
	int subptr = location % 8;

	int i;
	for (i = 0; i < length; i++) {
		if (subptr == 0) {
			buffer[i] = stream->stream[ptr+i];
		}
		else {
			uint8_t top_half    = stream->stream[ptr+i]   << subptr;
			uint8_t bottom_half = stream->stream[ptr+i+1] >> (8-subptr);

			buffer[i] = top_half | bottom_half;
		}
	}
}

void dump_bytes(struct bytestream *stream, int location, int length)
{
	uint8_t tmp[length/8];
	bytestream_get_location(stream, location, tmp, length/8);

	int i;
	for (i = 0; i < length/8; i++) {
		if (i % 16 == 0) {
			printf("\nDUMP %04x ", i);
		}
		else if (i % 8 == 0) {
			printf("   ");
		}
		printf(" %02x", tmp[i]);
	}
	printf("\n");
}


struct sync_mark { uint8_t bytes[6]; };
// This is 3x 0x41 on the data plane
struct sync_mark pre_mark = { { 0x44, 0x89, 0x44, 0x89, 0x44, 0x89 } };

uint8_t test_sync_patterns(struct bytestream *stream, int location, int debug_bool)
{
	int rc;
	uint8_t data[6];

	bytestream_get_location(stream, location, data, 6);
	rc = memcmp(data, &pre_mark, PRE_MARK_LEN_BYTES);
	if (rc == 0) {
		return MARKER_PRE;
	}

	return MARKER_UNKNOWN;
}

void bytestream_push(struct bytestream *stream, uint8_t val, int bits, uint8_t track_num, uint8_t side_num, uint32_t idx, double time_index)
{
	char buffer[9];
	buffer[8] = '\0';
	print_bin(buffer, stream->stream[stream->ptr], 8);


	int i;
	uint16_t slider = stream->recent[0] << bits;

	for (i = 1; i < STREAM_RECENT_WINDOW; i++) {
		slider = (slider << 8) | (stream->recent[i] << bits);
		uint8_t tmp = slider >> 8;
		stream->recent[i-1] = tmp;
	}
	stream->recent[i-1] = (0x00ff & slider) | val;

	if (stream->subptr + bits >= 8) {
		// The pattern is always N zeroes then a 1, so...

		uint8_t bits_remaining = 8 - stream->subptr;

		if (bits_remaining == bits) {
			uint8_t byte = stream->stream[stream->ptr];
			byte = byte | val;
			stream->stream[stream->ptr] = byte;
			stream->time_idx[stream->ptr] = time_index;
		}

		print_d_c_vals(stream->stream[stream->ptr], side_num, track_num, idx);

		stream->ptr++;
		stream->subptr = 0;
		bits -= bits_remaining;
	}

	if (stream->subptr + bits < 8) {
		val = val << (8 - stream->subptr - bits);

		uint8_t byte = stream->stream[stream->ptr];

		byte = byte | val;
		stream->stream[stream->ptr] = byte;
		stream->subptr += bits;
	}

	print_bin(buffer, stream->stream[stream->ptr], 8);
}

void separate_data_clock(uint8_t *data, uint8_t *d, uint8_t *c)
{
	*d    = ((data[0] & 0x40) << 1) |
		((data[0] & 0x10) << 2) |
		((data[0] & 0x04) << 3) |
		((data[0] & 0x01) << 4) |
		((data[1] & 0x40) >> 3) |
		((data[1] & 0x10) >> 2) |
		((data[1] & 0x04) >> 1) |
		 (data[1] & 0x01);

	*c    =  (data[0] & 0x80)       |
		((data[0] & 0x40) << 1) |
		((data[0] & 0x08) << 2) |
		((data[0] & 0x02) << 3) |
		((data[1] & 0x80) >> 4) |
		((data[1] & 0x40) >> 3) |
		((data[1] & 0x08) >> 2) |
		((data[1] & 0x02) >> 1);
}

int parse_id_record(struct sector *sector, struct bytestream *stream, int location)
{
	uint8_t data[ID_RECORD_LEN_BYTES];
	bytestream_get_location(stream, location, data, ID_RECORD_LEN_BYTES);

	uint8_t d, c;


// From 0xcdb4
// 0xFE, 0x00, 0x00, 0x03, 0x02 should have the value $AC0D
	log_dbg("-------------------");
	log_dbg("I have a PRE marker at position %08x", location);
	log_dbg("Going to parse ID record");


	uint16_t crc;
	crc = 0xcdb4;

	uint8_t  parity, t;


	separate_data_clock(data, &d, &c);
	if (d != 0xfe) {
		
		log_err("Header marker is not 0xFE! d:%02x c:%02x", d, c);
		return -1;
	}
	parity = d       ^ (crc >> 8);
	t      = parity  ^ (parity >> 4);
	crc = (crc << 8) ^ t ^ (t << 5) ^ (t << 12);


	separate_data_clock(data+2, &d, &c);
	if (d > 83) {
		log_err("Invalid track count in sector header: %02x", d);
		return -1;
	}
	sector->meta.track = d;

	parity = d       ^ (crc >> 8);
	t      = parity  ^ (parity >> 4);
	crc = (crc << 8) ^ t ^ (t << 5) ^ (t << 12);


	separate_data_clock(data+4, &d, &c);
	if (d > 1) {
		log_err("Invalid side count in sector header: %02x", d);
		return -1;
	}
	sector->meta.side = d;

	parity = d       ^ (crc >> 8);
	t      = parity  ^ (parity >> 4);
	crc = (crc << 8) ^ t ^ (t << 5) ^ (t << 12);


	separate_data_clock(data+6, &d, &c);
	if (d > 10) {
		log_err("Invalid sector count in sector header: %02x", d);
		return -1;
	}
	sector->meta.sector_num = d;

	parity = d       ^ (crc >> 8);
	t      = parity  ^ (parity >> 4);
	crc = (crc << 8) ^ t ^ (t << 5) ^ (t << 12);

	separate_data_clock(data+8, &d, &c);
	if (d > 4) {
		log_err("Invalid sector size in sector header: %02x", d);
		return -1;
	}
	sector->meta.size = SECTOR_LEN[d];

	parity = d       ^ (crc >> 8);
	t      = parity  ^ (parity >> 4);
	crc = (crc << 8) ^ t ^ (t << 5) ^ (t << 12);

	separate_data_clock(data+10, &d, &c);
	sector->meta.disk_crc = d << 8;
	separate_data_clock(data+12, &d, &c);
	sector->meta.disk_crc = sector->meta.disk_crc | d;

	sector->meta.calc_crc = crc;

	if (sector->meta.disk_crc != crc) {
		return -1;
	}


	return 0;
}


/* Parses a sequence of zeroes */
int parse_sync_mark(struct bytestream *stream, int location)
{
	int rc = 0;

	uint8_t data[2];
	uint8_t d, c;

	bytestream_get_location(stream, location, data, 2);
	separate_data_clock(data, &d, &c);

	while (d == 0x00) {
		rc++;
		location += 16;

		bytestream_get_location(stream, location, data, 2);
		separate_data_clock(data, &d, &c);
	}

	if (rc != 12) {
		log_dbg("This can legally be other values, but right now: I only parsed %u 0x00s", rc);
	}

	return rc;
}

/* parse a sequence of 0x4es */
int parse_gap_3a(struct bytestream *stream, int location)
{
	int rc = 0;

	uint8_t data[2];
	uint8_t d, c;

	bytestream_get_location(stream, location, data, 2);
	separate_data_clock(data, &d, &c);

	while (d == 0x4e) {
		rc++;
		location += 16;

		bytestream_get_location(stream, location, data, 2);
		separate_data_clock(data, &d, &c);
	}

	if (rc != 22) {
		log_dbg("This can legally be other values, but right now: I only parsed %u 0x4es", rc);
	}

	return rc;
}


int parse_gap_4(struct bytestream *stream, int location)
{
	int rc = 0;

	uint8_t data[2];
	uint8_t d, c;

	bytestream_get_location(stream, location, data, 2);
	separate_data_clock(data, &d, &c);

	while (d == 0x4e) {
		rc++;
		location += 16;

		bytestream_get_location(stream, location, data, 2);
		separate_data_clock(data, &d, &c);
	}

	if (rc != 40) {
		log_dbg("This can legally be other values, but right now: I only parsed %u 0x4es", rc);
	}

	return rc;
}

uint16_t calc_crc(uint8_t d, uint16_t crc_val)
{
	uint8_t  parity, t;
	parity = d       ^ (crc_val >> 8);
	t      = parity  ^ (parity >> 4);
	crc_val = (crc_val << 8) ^ t ^ (t << 5) ^ (t << 12);

	return crc_val;
}


int parse_data(struct sector *sector, struct bytestream *stream, int location, int length_bytes)
{
	int rc = 0;

	uint8_t data[2];
	uint8_t d, c;

	uint16_t crc_val = 0xcdb4;

	bytestream_get_location(stream, location, data, 2);
	separate_data_clock(data, &d, &c);
	crc_val = calc_crc(d, crc_val);

	location += 16;

	if (d != 0xfb) {
		log_err("Data byte leading into data sector should be 0xfb, but is %02x", d);
	}

	while (rc < length_bytes) {
		rc++;

		bytestream_get_location(stream, location, data, 2);
		separate_data_clock(data, &d, &c);

		location += 16;

		crc_val = calc_crc(d, crc_val);
	}

	uint8_t crc[2];
	bytestream_get_location(stream, location, crc, 2);
	separate_data_clock(crc, &d, &c);
	sector->data.disk_crc = d << 8;
	location += 16;

	bytestream_get_location(stream, location, crc, 2);
	separate_data_clock(crc, &d, &c);
	location += 16;
	sector->data.disk_crc = sector->data.disk_crc | d;

	sector->data.calc_crc = crc_val;

	rc += 1; // the 0xfb byte at the start
	rc += 2; // the CRC bytes at the end

	return rc;
}

void shift_bytestream(struct bytestream *old_stream, uint32_t start_bit, struct bytestream *new_stream)
{
	uint32_t bit = start_bit;
	for ( ; bit < (8*old_stream->ptr)+old_stream->subptr; bit += 8) {
		uint8_t tmp;
		double  timer;
		bytestream_get_location(old_stream, bit, &tmp, 1);
		bytestream_get_timer(old_stream, bit, &timer);
		bytestream_push(new_stream, tmp, 8, 0, 0, 0, timer);
	}

}


void summarise_and_log_read_status(struct sector_list sectors)
{
	struct sector *sector;
	int reads[MAX_SECTORS];
	memset(reads, 0, MAX_SECTORS*sizeof(int));
	LIST_FOREACH(sector, &sectors, next) {
		if (sector->meta.calc_crc == sector->meta.disk_crc &&
		    sector->data.calc_crc == sector->data.disk_crc) {
			reads[sector->meta.sector_num-1]++;
		}
		//total_reads[sector->meta.sector_num-1]++;
	}

	char reads_str[LINE_MAX];
	reads_str[0] = '\0';
	int i;
	for (i = 0; i < MAX_SECTORS; i++) {
		char *colour = NULL;
		if (reads[i] > 0) {
			colour = KGRN;
		}
		else {
			colour = KRED;
		}

		int len = strlen(reads_str);
		if (len > 0) {
			snprintf(reads_str+len, LINE_MAX-len-1, ", %s%2u%s", colour, i+1, KNRM);
		}
		else {
			snprintf(reads_str+len, LINE_MAX-len-1, "%s%2u%s", colour, i+1, KNRM);
		}
	}

	log_msg("READS: %s", reads_str);
}


void parse_data_stream(struct track *track)
{
	unsigned int i;
	unsigned int bit;

	char log_line[LINE_MAX];
	memset(log_line, '\0', LINE_MAX);
	char *sts_str;
	int   sts_str_len = LINE_MAX-1;

	struct bytestream *stream = track->stream;

	parser_state = UNSYNCED;
	i = 0;

	struct sector *sector     = NULL;
	bool           good_parse = false;

	while (parser_state != TRACK_COMPLETE) {
		switch (parser_state) {
		case UNSYNCED: {
			int found_marker = 0;

			log_dbg("Going to try to byte-align; range: 0000 .. %04x(%04x)", stream->ptr, stream->subptr);

			/* Scan forward to find a sector marker; use this to byte-align */
			for (bit = 0; bit < (8*stream->ptr)+stream->subptr; bit++) {
			//for (bit = i; bit < (8*stream->ptr)+stream->subptr; bit++) {
				uint8_t code = test_sync_patterns(stream, bit, 0);
				switch (code) {
				case MARKER_PRE: {
					log_dbg("I FOUND A PRE-INDEX MARKER AT BIT %04x", bit);
					struct bytestream *shifted_bytestream;
					bytestream_init(&shifted_bytestream);
					shift_bytestream(stream, bit, shifted_bytestream);

					bytestream_destroy(&stream);
					stream = shifted_bytestream;
					track->stream = shifted_bytestream;

					//i = bit;
					// Get out of here
					bit = (8*stream->ptr)+stream->subptr;
					found_marker = 1;

					parser_state = SEEKING_PRE_ID;

					break;
				}
				}
			}

			if (!found_marker) {
				log_msg("Nothing found in track");
				parser_state = TRACK_COMPLETE;
			}

			break;
		}

		case SEEKING_PRE_ID: {
			//stream = track->stream;
			log_dbg("I think I'm byte-aligned; at bit %04x in range: 0000 .. %04x", i, stream->ptr*8 + stream->subptr);

			for (; i < stream->ptr*8; i++) {
				uint8_t code = test_sync_patterns(stream, i, 1);
				//switch (code) {
				//case MARKER_PRE: {
				if (code == MARKER_PRE) {
					parser_state = FOUND_ID;
					i += PRE_MARK_LEN_BITS;
					break;
				}

				//if (code == MARKER_UNKNOWN) {
				//	log_dbg("Not byte-aligned; unsynchronised at location %04x", i);
				//	parser_state = UNSYNCED;
				//	i++;
				//	break;
				//}
			}

			if (i >= stream->ptr*8) {
				parser_state = TRACK_COMPLETE;
			}
			if (sector == NULL) {
				sector_init(&sector);
			}
			else {
				log_err("ERROR: attempted to re-init a sector");
			}
			break;
		}

		case FOUND_ID: {
			//parse_sector(stream, &i);
			log_dbg("IN:  FOUND_ID: i: %x", i);
			int rc = parse_id_record(sector, stream, i);
			if (rc != 0) {
				/* reset */
				parser_state = SEEKING_PRE_ID;
				free(sector->data.data);
				free(sector);
				sector = NULL;

				break;
			}

			i += ID_RECORD_LEN_BITS;
			parser_state = SEEKING_POST_ID;
			rc = sprintf(log_line, "[Phase 2: side:%02u, track:%02u, sector:%02u, size:%u]",
					sector->meta.side,
					sector->meta.track,
					sector->meta.sector_num,
					sector->meta.size);
			if (rc < 0) {
				log_err("sprintf() error?");
				break;
			}

			char sts_str[80];
			sts_str[0] = '\0';
			sprintf(sts_str, "[ID Seg: %sOK%s]", KGRN, KNRM);
			strncat(log_line, sts_str, sts_str_len);
			sts_str_len -= strlen(sts_str);

			log_dbg("OUT: FOUND_ID: i: %x", i);
			break;
		}

		case SEEKING_POST_ID: {
			int rc = parse_gap_3a(stream, i);
			i += rc * 8 * 2;
			log_dbg("[parsed ID record post-mark: %u 0x4e's]", rc);

			rc = parse_sync_mark(stream, i);
			log_dbg("[parsed zero gap: %u 0x00's]", rc);
			i += rc * 8 * 2;

			parser_state = SEEKING_DATA;

			break;
		}

		case SEEKING_DATA: {
			uint8_t code = test_sync_patterns(stream, i, 1);
			switch (code) {
			case MARKER_PRE: {
				parser_state = FOUND_ID;
				i += PRE_MARK_LEN_BITS;
				parser_state = FOUND_DATA;
				break;
			}
			default: {
				log_err("Unknown sync pattern! [%s]", log_line);
				parser_state = SEEKING_PRE_ID;

				free(sector->data.data);
				free(sector);
				sector = NULL;
			}
			}

			break;
		}

		case FOUND_DATA: {
			int rc = parse_data(sector, stream, i, 512);
			log_dbg("[parsed data field; %u bytes", rc);
			if (rc != 512 + 1 + 2) {
				if (sector->data.disk_crc != sector->data.calc_crc) {
					char tmp[LINE_MAX];
					memset(tmp, '\0', LINE_MAX);
					sprintf(tmp, "[Data Seg ERR CRC mismatch: %04x != %04x]",
						sector->data.disk_crc, sector->data.calc_crc);
					strncat(log_line, tmp, sts_str_len);
					sts_str_len -= strlen(tmp);
				}
				else {
					sts_str = "[Data Seg ERR %02x bytes?]";
					char tmp[LINE_MAX];
					memset(tmp, '\0', LINE_MAX);
					sprintf(tmp, "[Data Seg ERR %02x bytes?]", rc);
					strncat(log_line, sts_str, sts_str_len);
					sts_str_len -= strlen(sts_str);
				}
			}
			else {
				sts_str = "[Data Seg OK]";
				strncat(log_line, sts_str, sts_str_len);
				sts_str_len -= strlen(sts_str);
			}
			i += rc * 8 * 2;

			parser_state = SEEKING_POST_DATA;

			break;
		}

		case SEEKING_POST_DATA: {
			int rc = parse_gap_4(stream, i);
			log_dbg("[parsed %u 0x4e's]", rc);
			if (rc != 40) {
				log_dbg("This can legally be other values, but right now: I only parsed %u 0x4es", rc);
			}
			i += rc * 8 * 2;

			rc = parse_sync_mark(stream, i);
			log_dbg("[parsed zero gap: %u 0x00's]", rc);
			i += rc * 8 * 2;

			log_msg("%s", log_line);

			/* store */
			LIST_INSERT_HEAD(&track->sectors, sector, next);

			/* reset */
			sector = NULL;
			parser_state = SEEKING_PRE_ID;

			break;
		}

		default: {
			log_err("welp");
		}
		}

	}

	if (sector != NULL && !good_parse) {
		free(sector->data.data);
		free(sector);
		sector = NULL;
	}


	summarise_and_log_read_status(track->sectors);


}

void count_flux_sum(struct track *track, uint32_t index, uint32_t next_index, uint32_t pass, uint32_t *flux_sum)
{
	/* Method to calculate the time between two indices:
	 * page 10: It can also be calculated by summing all the flux reversal
	 * values that we recorded since the previous index, adding the Sample
	 * Counter value at which the index was detected (see Sample Counter
	 * field in Index Block) and subtracting the Sample Counter value of
	 * the previous index.
	 */
	while (index < next_index && index < track->flux_array_idx) {
		index++;
		//double flux_us = track->flux_array[index] / track->sample_clock;
		*flux_sum += track->flux_array[index];
	}

	// Decoder must manually insert an empty flux at the end.
	if (index != next_index) {
		log_err("MFMTRACK [Phase 1: S:%x, T:%02u, PASS:%u, next_index:%5x] NOT FOUND, AT END? %x %x",
			track->side, track->track, pass, next_index, index-1, next_index);
	}
}

int mfm_decode_passes(struct track *track, uint32_t index, uint32_t next_index)
{
	//uint32_t flux_count = 0;

	if (index >= track->flux_array_idx) {
		log_err("MFMTRACK [S:%x, T:%02u] WARNING: SEEK ERROR ON STREAM_POS %x", track->side, track->track, index);
		return index;
	}

	struct bytestream *stream;
	bytestream_init(&stream);
	track->stream = stream;

	double time_index = 0.0;

	while (index < next_index && index < track->flux_array_idx) {
		double flux_us = track->flux_array[index] / track->sample_clock;

		time_index += flux_us;
		index++;

//		*flux_sum += track->flux_array[index];

		if (flux_us > 0.0000034 && flux_us < 0.0000046) {
			bytestream_push(stream, 0x00000001, 2, track->side, track->track, index, time_index);
		}
		else if (flux_us > 0.0000054 && flux_us < 0.0000066) {
			bytestream_push(stream, 0x00000001, 3, track->side, track->track, index, time_index);
		}
		else if (flux_us > 0.0000074 && flux_us < 0.0000086) {
			bytestream_push(stream, 0x00000001, 4, track->side, track->track, index, time_index);
		}
//		else {
//			log_dbg("[side:%u, track:%u] Trying to parse %0.7f", track->side, track->track, flux_us);
//		}
	}

	// Decoder must manually insert an empty flux at the end.
	if (index != next_index) {
		log_err("MFMTRACK [Phase 1: S:%x, T:%02u, next_index:%5x] NOT FOUND, AT END? %x %x",
			track->side, track->track, next_index, index-1, next_index);
	}

	return index;
}



int decode_flux_to_mfm(struct track *track)
{
//	uint32_t pass;

//	uint32_t last_index_counter  = 0;
//	uint32_t last_sample_counter = 0;

//	for (pass = 0; pass < track->indices_idx; pass++) {
//		uint32_t flux_sum       = 0;
//		uint32_t index_pos      = track->indices[pass].stream_pos;
//		uint32_t next_index_pos = track->indices[pass+1].stream_pos;
//
//		log_dbg("MFM [S:%u, T:%02u, PASS:%x] INDEX: %05x %0.3f [%0.3f:%0.3f:%0.3f] %x",
//			track->side, track->track, pass,
//			track->indices[pass].stream_pos,
//			track->indices[pass].sample_counter                  / track->sample_clock * 1000 * 1000,
//			track->flux_array[track->indices[pass].stream_pos-1] / track->sample_clock * 1000 * 1000,
//			track->flux_array[track->indices[pass].stream_pos]   / track->sample_clock * 1000 * 1000,
//			track->flux_array[track->indices[pass].stream_pos+1] / track->sample_clock * 1000 * 1000,
//			track->indices[pass].index_counter);
//
//		count_flux_sum(track, index_pos, next_index_pos, pass, &flux_sum);
//
//		log_dbg("MFM [S:%x, T:%02u, PASS:%x] SAMPLE CLOCK: %0.3fus",
//			track->side, track->track, pass,
//			track->indices[pass].sample_counter / track->sample_clock * 1000 * 1000);
//
//		double idx_clock = pass ? (track->indices[pass].index_counter - last_index_counter)/track->index_clock : 0.0;
//
//		log_dbg("MFM [S:%x, T:%02u, PASS:%x] INDEX CLOCK:  %f (%f)",
//			track->side, track->track, pass,
//			track->indices[pass].index_counter/track->index_clock,
//			idx_clock);
//
//		uint32_t diff = flux_sum - last_sample_counter + track->indices[pass].sample_counter;
//		log_dbg("MFM [S:%x, T:%02u, PASS:%u] Space between indices: %0.3fms; %0.3f RPM",
//			track->side, track->track, pass,
//			diff/track->sample_clock * 1000,
//			60/(diff/track->sample_clock));
//	}

	uint32_t pass = track->indices_idx - 1;

	uint32_t first_index = track->indices[0].stream_pos;
	uint32_t last_index  = track->indices[pass-1].stream_pos;

	log_dbg("MFM [S:%x, T:%02u] Gonna decode flux stream: %x -- %x", track->side, track->track, first_index, last_index);
	mfm_decode_passes(track, first_index, last_index);

	log_dbg("MFM [S:%x, T:%02u] Gonna parse the data out", track->side, track->track);
	parse_data_stream(track);

	return 0;
}

