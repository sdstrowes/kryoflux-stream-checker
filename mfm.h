#ifndef __MFM_H__
#define __MFM_H__

#include "stream.h"

#define MARKER_UNKNOWN            0
#define MARKER_PRE                1
#define PRE_MARK_LEN_BYTES  6
#define PRE_MARK_LEN_BITS  48
#define ID_RECORD_LEN_BYTES      14
#define ID_RECORD_LEN_BITS      112

int decode_pass(struct track *track, uint32_t index, uint32_t next_index, uint32_t pass, uint32_t *flux_sum);

int decode_flux_to_mfm(struct track *track);

void bytestream_destroy(struct bytestream **s);


#endif

