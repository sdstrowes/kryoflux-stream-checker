#include "disk-analysis-log.h"
#include "kf-oob.h"
#include "kf-info.h"
#include <string.h>

#define OOB_INVALID    0x00
#define OOB_STREAMINFO 0x01
#define OOB_INDEX      0x02
#define OOB_STREAMEND  0x03
#define OOB_KFINFO     0x04
#define OOB_EOF        0x0d

#define OOB_STREAMINFO_SIZE 0x0008
#define OOB_INDEX_SIZE      0x000C
#define OOB_STREAMEND_SIZE  0x0008
#define OOB_EOF_SIZE        0x0d0d // not meaningful


// 0x00 invalid
// type & size == 0
int parse_oob_invalid(uint16_t size)
{
	if (size != 0x0000) {
		log_err("no idea what I'm doing with an OOB invalid");
		return -1;
	}

	return 0;
}

// 0x01 stream info
// - size == 8 bytes
// - 4 bytes stream position
// - 4 bytes transfer time
int parse_streaminfo(FILE *f, uint16_t size, uint32_t *stream_pos)
{
	int rc;

	uint32_t oob_stream_pos;
	uint32_t oob_transfer_time;

	if (size != OOB_STREAMINFO_SIZE) {
		log_err("OOB StreamInfo block has incorrect length: %04x (should be %04x)", size, OOB_STREAMINFO_SIZE);
		return -1;
	}

	rc = fread(&oob_stream_pos, 4, 1, f);
	if (rc < 1) {
		log_err("OOB StreamInfo, reading stream position failed");
		return -1;
	}
	log_dbg("oob_stream_pos: %08x", oob_stream_pos);

	rc = fread(&oob_transfer_time, 4, 1, f);
	if (rc < 1) {
		log_err("OOB StreamInfo, reading transfer time failed");
		return -1;
	}

	log_dbg("[%5x] Stream Info: transfer time:%4ums", *stream_pos, oob_transfer_time);

	if (*stream_pos != oob_stream_pos) {
		log_dbg("[%5x] Stream Info: pos:%08x; transfer time:%4ums", *stream_pos, oob_stream_pos, oob_transfer_time);
		log_err("stream_pos[%05x] does not match information in OOB info block[%05x]", *stream_pos, oob_stream_pos);
		return -1;
//		log_err("WARNING: stream_pos:%06x does not match oob:%06x; resetting",
//			*stream_pos, oob_stream_pos);
//		*stream_pos = oob_stream_pos;
	}

	rc = fread(&oob_transfer_time, 4, 1, f);
	printf("What's next? %08x\n", oob_transfer_time);
	rc = fread(&oob_transfer_time, 4, 1, f);
	printf("What's next? %08x\n", oob_transfer_time);
	rc = fread(&oob_transfer_time, 4, 1, f);
	printf("What's next? %08x\n", oob_transfer_time);
	rc = fread(&oob_transfer_time, 4, 1, f);
	printf("What's next? %08x\n", oob_transfer_time);
	rc = fread(&oob_transfer_time, 4, 1, f);
	printf("What's next? %08x\n", oob_transfer_time);
	rc = fread(&oob_transfer_time, 4, 1, f);
	printf("What's next? %08x\n", oob_transfer_time);
	rc = fread(&oob_transfer_time, 4, 1, f);
	printf("What's next? %08x\n", oob_transfer_time);
	rc = fread(&oob_transfer_time, 4, 1, f);
	printf("What's next? %08x\n", oob_transfer_time);
	rc = fread(&oob_transfer_time, 4, 1, f);
	printf("What's next? %08x\n", oob_transfer_time);
	rc = fread(&oob_transfer_time, 4, 1, f);
	printf("What's next? %08x\n", oob_transfer_time);

	return 0;
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

// 0x02 parse index
// - size == 12 bytes
// - 4 bytes stream position
// - 4 bytes sample counter
// - 4 bytes index counter
int parse_oob_disk_index(FILE *f, struct track *track, uint16_t size, uint32_t *stream_pos)
{
	int rc;

	uint32_t oob_stream_pos;
	uint32_t oob_sample_counter;
	uint32_t oob_index_counter;

	if (size != OOB_INDEX_SIZE) {
		log_err("OOB Index block has incorrect length: %04x (should be %04x)", size, OOB_INDEX_SIZE);
		return -1;
	}

	rc = fread(&oob_stream_pos, 4, 1, f);
	if (rc < 1) {
		log_err("OOB Index, reading stream position failed");
		return -1;
	}

	rc = fread(&oob_sample_counter, 4, 1, f);
	if (rc < 1) {
		log_err("OOB Index, reading sample counter failed");
		return -1;
	}

	rc = fread(&oob_index_counter, 4, 1, f);
	if (rc < 1) {
		log_err("OOB Index, reading index counter failed");
		return -1;
	}

	printf("SECTION OOB INDEX oob_stream_pos:%08x sample_counter:%08x index_counter:%08x\n",
		oob_stream_pos, oob_sample_counter, oob_index_counter);
	append_index(track, oob_stream_pos, oob_sample_counter, oob_index_counter);

	log_dbg("[%5x] Index block: stream_pos:%08x sample_counter:%08x index_counter:%08x",
		*stream_pos, oob_stream_pos, oob_sample_counter, oob_index_counter);

	return 0;
}

// 0x03
int parse_oob_stream_end(FILE *f, uint16_t size, uint32_t *stream_pos)
{
	int rc;
	uint32_t oob_stream_pos;
	uint32_t oob_result_code;

	if (size != 0x0008) {
		log_err("Error parsing stream end block: size %04x", size);
		return -1;
	}

	rc = fread(&oob_stream_pos, 4, 1, f);
	if (rc < 1) {
		return -1;
	}

	rc = fread(&oob_result_code, 4, 1, f);
	if (rc < 1) {
		return -1;
	}

	log_dbg("[%5x] Stream end: pos:%08x result_code:%08x", *stream_pos, oob_stream_pos, oob_result_code);

	return 0;
}



int parse_oob(FILE *f, struct track *track, uint32_t *stream_pos)
{
	int rc;
	uint8_t type;
	uint16_t size;

	rc = fread(&type, 1, 1, f);
	if (rc < 1) {
		log_err("Error when reading OOB type code");
		return -1;
	}

	rc = fread(&size, 2, 1, f);
	if (rc < 1) {
		log_err("Error when reading OOB payload size");
		return -1;
	}

	switch (type) {
	case OOB_INVALID: {
		log_dbg("Parse OOB type %02x: %s", type, "invalid");
		rc = parse_oob_invalid(size);
		return rc;
	}

	case OOB_STREAMINFO: {
		log_dbg("Parse OOB type %02x: %s", type, "stream read");
		rc = parse_streaminfo(f, size, stream_pos);
		return rc;
	}

	case OOB_INDEX: {
		log_dbg("Parse OOB type %02x: %s", type, "index");
		rc = parse_oob_disk_index(f, track, size, stream_pos);
		return rc;
	}

	case OOB_STREAMEND: {
		log_dbg("Parse OOB type %02x: %s", type, "stream end");
		rc = parse_oob_stream_end(f, size, stream_pos);
		return rc;
	}

	case OOB_KFINFO: {
		printf("SECTION OOB [%02x] kfinfo\n", type);
		log_dbg("Parse OOB type %02x: %s", type, "kfinfo");
		struct kf_info info;
		memset(&info, 0, sizeof(info));
		parse_kf_info(f, size, &info);
		log_kf_info(&info);
		return 0;
	}

	// - size == 0x0d0d, not useful
	case OOB_EOF: {
		log_dbg("Parse OOB type %02x: %s", type, "index");

		if (size != OOB_INDEX_SIZE) {
			log_err("OOB EOF block has incorrect length: %04x (should be %04x)", size, OOB_EOF_SIZE);
			return -1;
		}

		log_dbg("[%5x] EOF", *stream_pos);
		return 0;
	}

	default: {
		printf("SECTION OOB [%02x] default\n", type);
		log_dbg("Parse OOB type %02x: %s", type, "unknown");
		log_err("Unknown OOB type %x", type);
		return -1;
	}
	}

	// should not reach here
	return -1;
}

