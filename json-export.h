#ifndef __JSON_EXPORT_H__
#define __JSON_EXPORT_H__

#include "atari-fs.h"
#include "disk-streams.h"

/* Writes <out_dir>/disk.json (side/track summary: mfm + filesystem layers,
 * plus a coarse flux overview for the whole-disk view) and one
 * <out_dir>/flux/<side>-<track>.json per track (full-resolution raw flux
 * ticks for every revolution, fetched on demand by the viewer). bpb/fat may
 * be NULL if no filesystem was recognised, in which case the filesystem
 * layer is emitted empty. */
int export_disk_json(struct disk_streams *disk, struct disk *disk_data,
                      struct bpb *bpb, struct fat *fat, const char *out_dir);

#endif
