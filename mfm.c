#include <arpa/inet.h>
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

	memset(stream->stream,   0, sizeof(stream->stream));
	memset(stream->time_idx, 0, sizeof(stream->time_idx));
//	memset(stream->recent,   STREAM_RECENT_WINDOW, sizeof(stream->recent));
	stream->ptr = 0;
//	stream->subptr = 0;

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
	(void)side; (void)track; (void)idx;

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
// This is 3x 0xA1 on the data plane
struct sync_mark pre_mark = { { 0x44, 0x89, 0x44, 0x89, 0x44, 0x89 } };

uint8_t test_sync_patterns(struct bytestream *stream, int location, bool debug)
{
	(void)debug;
	int rc;
	uint8_t data[6];

	bytestream_get_location(stream, location, data, 6);
	rc = memcmp(data, &pre_mark, PRE_MARK_LEN_BYTES);
	if (rc == 0) {
		return MARKER_PRE;
	}

	return MARKER_UNKNOWN;
}

void print_hex(char *buffer, uint8_t *val, int n)
{
	int i;
	char *ptr = buffer;
	for (i = 0; i < n; i++) {
    /* "sprintf" converts each byte in the "buf" array into a 2 hex string
     * characters appended with a null byte, for example 10 => "0A\0".
     *
     * This string would then be added to the output array starting from the
     * position pointed at by "ptr". For example if "ptr" is pointing at the 0
     * index then "0A\0" would be written as output[0] = '0', output[1] = 'A' and
     * output[2] = '\0'.
     *
     * "sprintf" returns the number of chars in its output excluding the null
     * byte, in our case this would be 2. So we move the "ptr" location two
     * steps ahead so that the next hex string would be written at the new
     * location, overriding the null byte from the previous hex string.
     *
     * We don't need to add a terminating null byte because it's been already 
     * added for us from the last hex string. */  
	    ptr += sprintf(ptr, "%02X", val[i]);
	}
}

void bytestream_push(struct bytestream *stream, uint8_t val, int bits, uint8_t track_num, uint8_t side_num, uint32_t idx, double time_index)
{
	(void)track_num; (void)side_num; (void)idx; (void)time_index;

	uint32_t index = stream->ptr / 8;
	uint32_t subidx = stream->ptr % 8;

//	log_dbg("bytestream_push: ptr:%u index:%u  subidx:%u", stream->ptr, index, subidx);

	uint16_t tmpslice;
	memcpy(&tmpslice, (stream->stream)+index, sizeof(uint16_t));
	uint16_t slice = htons(tmpslice);

	uint16_t tmpval = val << (16 - bits);
	slice = slice | (tmpval >> subidx);

	tmpslice = htons(slice);
	memcpy( (stream->stream)+index, &tmpslice, sizeof(uint16_t));

	//debug
//	{
//		char dbg_buffer[index * 2 + 2];
//		dbg_buffer[index * 2 + 2] = '\0';
//		print_hex(dbg_buffer, stream->stream, index + 1);
//		log_dbg("stream--> %s", dbg_buffer);
//	}

	stream->ptr += bits;

//	print_bin(dbg_buffer, stream->stream[stream->ptr], 8);
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
		((data[0] & 0x20) << 1) |
		((data[0] & 0x08) << 2) |
		((data[0] & 0x02) << 3) |
		((data[1] & 0x80) >> 4) |
		((data[1] & 0x20) >> 3) |
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
	if (d >= 4) {
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

	log_msg("ID record: track:  %u", sector->meta.track);
	log_msg("ID record: side:   %u", sector->meta.side);
	log_msg("ID record: sector: %u", sector->meta.sector_num);
	log_msg("ID record: size:   %u", sector->meta.size);

	if (sector->meta.disk_crc != crc) {
		log_err("ID record: CRC mismatch; expected %x, got %x", sector->meta.calc_crc, sector->meta.disk_crc);
		return -1;
	}

	log_msg("ID record: CRC %04x OK", sector->meta.disk_crc);
	return 0;
}



uint16_t calc_crc(uint8_t d, uint16_t crc_val)
{
	uint8_t  parity, t;
	parity = d       ^ (crc_val >> 8);
	t      = parity  ^ (parity >> 4);
	crc_val = (crc_val << 8) ^ t ^ (t << 5) ^ (t << 12);

	return crc_val;
}


int parse_data(struct disk *disk, struct sector *sector, struct bytestream *stream, int location, int length_bytes)
{
	(void)disk;
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

	uint8_t *data_bytes = (uint8_t *)calloc(1, length_bytes);


	while (rc < length_bytes) {

		bytestream_get_location(stream, location, data, 2);
		separate_data_clock(data, &d, &c);

		log_msg("DATA  %02x/%02x/%02x loc:%x] rc:%u length:%u: data:%02x",
			sector->meta.side,
			sector->meta.track,
			sector->meta.sector_num,
			location,
			rc, length_bytes,
			d);

		//data[rc] = d;

		data_bytes[rc] = d;

		location += 16;

		crc_val = calc_crc(d, crc_val);
		rc++;
	}

	sector->data.data     = data_bytes;
	sector->data.data_len = length_bytes;

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
	log_dbg("called shift_bytestream: doin this janky thing");

	uint32_t bit = start_bit;
	for ( ; bit < old_stream->ptr; bit += 8) {
		uint8_t tmp;
		double  timer;
		bytestream_get_location(old_stream, bit, &tmp, 1);
		bytestream_get_timer(old_stream, bit, &timer);
		bytestream_push(new_stream, tmp, 8, 0, 0, 0, timer);
	}

	log_dbg("exiting shift_bytestream: todo: check whether we capture last byte");
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


void parse_data_stream(struct disk *disk, struct track *track)
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

	struct sector *sector = NULL;

	while (parser_state != TRACK_COMPLETE) {
		switch (parser_state) {
		case UNSYNCED: {
			log_dbg("parser_state: UNSYNCED (%u)", parser_state);
			int found_marker = 0;

			log_dbg("Going to try to byte-align; bitptr %04x", stream->ptr);

			/* Scan forward to find a sector marker; use this to byte-align */
			for (bit = 0; bit < stream->ptr; bit++) {
				uint8_t code = test_sync_patterns(stream, bit, false);
				switch (code) {
				case MARKER_PRE: {
					log_dbg("Found: Gap 2a (pre-index marker) at bit %04x", bit);
					struct bytestream *shifted_bytestream;
					bytestream_init(&shifted_bytestream);
					shift_bytestream(stream, bit, shifted_bytestream);

					bytestream_destroy(&stream);
					stream = shifted_bytestream;
					track->stream = shifted_bytestream;

					//i = bit;
					// Get out of here
					bit = stream->ptr;
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
			log_dbg("parser_state [%u] seeking pre ID", parser_state);
			//stream = track->stream;
			log_dbg("I think I'm byte-aligned; at bit %04x", stream->ptr);

			for (; i < stream->ptr; i++) {
				uint8_t code = test_sync_patterns(stream, i, true);
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

			if (i >= stream->ptr) {
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
			log_dbg("parser_state [%u] found ID", parser_state);
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
			parser_state = SEEKING_DATA;
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

		case SEEKING_DATA: {
			log_dbg("parser_state [%u] seeking data", parser_state);
			/* Scan forward for the data sync mark. Cap at 1024 bits
			 * (~gap 3a + sync zeros) so a missing data mark doesn't
			 * overshoot into the next sector's ID mark. */
			uint32_t scan_end = i + 1024;
			if (scan_end > stream->ptr) scan_end = stream->ptr;
			int found = 0;
			uint32_t scan;
			for (scan = i; scan < scan_end; scan++) {
				if (test_sync_patterns(stream, scan, false) == MARKER_PRE) {
					i = scan + PRE_MARK_LEN_BITS;
					parser_state = FOUND_DATA;
					found = 1;
					break;
				}
			}
			if (!found) {
				log_err("Data sync mark not found [%s]", log_line);
				parser_state = SEEKING_PRE_ID;
				free(sector->data.data);
				free(sector);
				sector = NULL;
			}
			break;
		}

		case FOUND_DATA: {
			log_dbg("parser_state [%u] found data", parser_state);
			int rc = parse_data(disk, sector, stream, i, sector->meta.size);
			log_dbg("[parsed data field; %u bytes", rc);
			if (rc != sector->meta.size + 1 + 2) {
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
			log_msg("%s", log_line);
			/* Store sector; SEEKING_PRE_ID will scan forward through gap 4. */
			LIST_INSERT_HEAD(&track->sectors, sector, next);
			sector = NULL;
			parser_state = SEEKING_PRE_ID;
			break;
		}

		default: {
			log_dbg("parser_state: UNKNOWN (%u)", parser_state);
			log_err("welp");
		}
		}
	}
	log_dbg("parser_state [%u] exiting, track complete", parser_state);

	if (sector != NULL) {
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
	while (index < next_index && index < track->flux_buf_idx) {
		//double flux_us = track->stream_buf[index] / track->sample_clock;
		*flux_sum += track->flux_buffer[index].val;
		index++;
	}

	// Decoder must manually insert an empty flux at the end.
	if (index != next_index) {
		log_err("MFMTRACK [Count flux sum: S:%x, T:%02u, PASS:%u, next_index:%5x] NOT FOUND, AT END? %x %x",
			track->side, track->track, pass, next_index, index-1, next_index);
	}
}

int mfm_decode_passes(struct track *track, uint32_t index, uint32_t next_index)
{
	//uint32_t flux_count = 0;

	if (index >= track->flux_buf_idx) {
		log_err("MFMTRACK [S:%x, T:%02u] WARNING: SEEK ERROR ON STREAM_POS %x", track->side, track->track, index);
		return index;
	}

	if (track->stream != NULL) {
		bytestream_destroy(&track->stream);
	}
	struct bytestream *stream;
	bytestream_init(&stream);
	track->stream = stream;

	double time_index = 0.0;

	while (index < next_index && index < track->flux_buf_idx) {
		double flux_us = track->flux_buffer[index].val / track->sample_clock;

		time_index += flux_us;
		index++;

//		*flux_sum += track->stream_buf[index];

		if (flux_us > 0.0000034 && flux_us < 0.0000046) {
			bytestream_push(stream, 0x00000001, 2, track->side, track->track, index, time_index);
		}
		else if (flux_us > 0.0000054 && flux_us < 0.0000066) {
			bytestream_push(stream, 0x00000001, 3, track->side, track->track, index, time_index);
		}
		else if (flux_us > 0.0000074 && flux_us < 0.0000086) {
			bytestream_push(stream, 0x00000001, 4, track->side, track->track, index, time_index);
		}
		else {
			log_dbg("[side:%u, track:%u] Trying to parse %0.7f", track->side, track->track, flux_us);
		}
	}

	// Decoder must manually insert an empty flux at the end.
	if (index != next_index) {
		log_err("MFMTRACK [decoder: S:%x, T:%02u, next_index:%5x] NOT FOUND, AT END? %x %x",
			track->side, track->track, next_index, index-1, next_index);
	}

	return index;
}



int decode_flux_to_mfm(struct disk *disk, struct track *track)
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
//			track->stream_buf[track->indices[pass].stream_pos-1] / track->sample_clock * 1000 * 1000,
//			track->stream_buf[track->indices[pass].stream_pos]   / track->sample_clock * 1000 * 1000,
//			track->stream_buf[track->indices[pass].stream_pos+1] / track->sample_clock * 1000 * 1000,
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

// For each Index Signal:
//  * The Stream Position points to the position of the next flux reversal in the stream buffer.
//  * The Sample Counter value indicates how far from the beginning of the previous flux reversal the index is detected.

	log_dbg("Indices idx: %u, indices max: %u", track->indices_idx, track->indices_max);
	uint i;
//	for (i = 0; i < track->indices_max; i++) {
	for (i = 0; i + 1 < track->indices_idx; i++) {
		log_dbg("foo: next flux reversal:%08x distance fro last flux reversal:%08x index_counter:%08x",
			track->index[i].stream_pos,
			track->index[i].sample_counter,
			track->index[i].index_counter);

		uint32_t index_counter_delta = track->index[i+1].index_counter - track->index[i].index_counter;
		log_dbg("idx counter delta: %u, clock: %f", index_counter_delta, index_counter_delta / track->index_clock);

		uint32_t first_index = track->index[i].stream_pos;
		uint32_t last_index  = track->index[i+1].stream_pos;

		log_dbg("MFM [S:%x, T:%02u] Gonna decode flux stream: %x -- %x", track->side, track->track, first_index, last_index);
		mfm_decode_passes(track, first_index, last_index);
		log_dbg("MFM [S:%x, T:%02u] Gonna parse the data out", track->side, track->track);
		parse_data_stream(disk, track);
	}

	return 0;
}

