#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "disk-analysis-log.h"
#include "fluxstream.h"
#include "kf-oob.h"

void append_isb(struct track *track, uint8_t value, uint32_t stream_pos)
{
	if (stream_pos >= track->isb_max - 1) {
		uint32_t old_max = track->isb_max;
		track->isb_max *= 2;

		uint8_t *tmp = (uint8_t *)calloc(track->isb_max, sizeof(uint8_t));
		if (tmp == NULL) {
			log_err("calloc failed in append_isb");
			exit(1);
		}
		if (track->isb != NULL) {
			memcpy(tmp, track->isb, old_max*sizeof(uint8_t));
			free(track->isb);
		}
		track->isb = tmp;
	}

	track->isb[stream_pos] = value;

	// idx becomes a marker for the last entry in the array
	track->isb_idx = stream_pos;
}

int parse_buffers_from_raw(char *fn, struct track *track, uint8_t side, uint8_t track_num)
{
	FILE *f;
	uint32_t stream_pos = 0;

	f = fopen(fn, "r");
	if (f == NULL) {
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
	track->index = (struct index *)malloc(sizeof(struct index)*track->indices_max);

	track->flux_buf_idx = 0;
	track->flux_buf_max = 1;
	track->flux_buffer = NULL;

	track->isb_idx = 0;
	track->isb_max = 1;
	track->isb     = NULL;

//	LIST_INIT(&(track->sectors));

	log_dbg("CLOCKS: %.10f %.10f %.10f",
		track->master_clock, track->sample_clock, track->index_clock);


	uint8_t encoding_marker;
	int rc;
	bool eod = false;
	bool ovl16 = false;

	while (!eod) {
		rc = fread(&encoding_marker, 1, 1, f);
		if (rc < 1) {
			log_err("fread() fail");
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
			log_dbg("SECTIONNNN [%02x] flux2", encoding_marker);
			uint8_t val;
			rc = fread(&val, 1, 1, f);
			if (rc < 1) {
				log_err("fread() fail");
				exit(1);
			}
			append_isb(track, encoding_marker, stream_pos);   // the value from the header
			append_isb(track,             val, stream_pos+1); // the value in the next byte

			stream_pos += 2;
			break;
		}
		// one-byte no-op
		case 0x08: {
			log_dbg("SECTION [%02x] no-op 1", encoding_marker);
			append_isb(track, encoding_marker, stream_pos);   // the value from the header
			stream_pos += 1;
			break;
		}
		// two-byte no-op
		case 0x09: {
			log_dbg("SECTION [%02x] no-op 2", encoding_marker);
			rc = fseek(f, 1, SEEK_CUR);
			if (rc != 0) {
				log_err("fseek() failed at pos %u: \"%s\"", stream_pos, strerror(errno));
				exit(1);
			}
			append_isb(track, encoding_marker, stream_pos);     // the value from the header
			append_isb(track, 0,               stream_pos+1);   // no-op; zero?
			stream_pos += 2;
			break;
		}
		// three-byte no-op; seek forward two additional bytes
		case 0x0a: {
			log_dbg("SECTION [%02x] no-op 3", encoding_marker);
			rc = fseek(f, 2, SEEK_CUR);
			if (rc != 0) {
				log_err("fseek() failed at pos %u: \"%s\"", stream_pos, strerror(errno));
				exit(1);
			}
			append_isb(track, encoding_marker, stream_pos);     // the value from the header
			append_isb(track,               0, stream_pos+1);   // no-op; zero?
			append_isb(track,               0, stream_pos+2);   // no-op; zero?
			stream_pos += 3;
			break;
		}
		// ovl16 ("overflow")
		case 0x0b: {
			log_dbg("SECTION [%02x] Overflow16, next flux block should be += 0x10000?", encoding_marker);
			append_isb(track, encoding_marker, stream_pos);   // the value from the header
			stream_pos += 1;
			break;
		}
		case 0x0c: {
			log_dbg("SECTION [%02x] flux3", encoding_marker);
			uint8_t val1, val2;
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

			append_isb(track, encoding_marker, stream_pos);   // the value from the header
			append_isb(track,            val1, stream_pos+1); // the value in the next byte
			append_isb(track,            val2, stream_pos+2); // the value in the next byte

			stream_pos += 3;
			break;
		}
		case 0x0d: {
			rc = parse_oob(f, track, &stream_pos);
			if (rc == 1) {
				eod = true;
			}
			else if (rc > 1) {
				log_err("Error parsing OOB block");
				exit(1);
			}
			else if (rc == -1) {
				log_err("Error parsing OOB block but continuing");
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

				append_isb(track, encoding_marker, stream_pos);   // the value from the header
			}
			else {
				log_err("Error: Unknown block type %x", encoding_marker);
			}
			stream_pos += 1;
		}
		}
	}

	fclose(f);

	return 0;
}

