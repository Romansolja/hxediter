#ifndef FILEOPS_H
#define FILEOPS_H

#include "platform.h"

// Single fopen chokepoint so any future per-open policy (umask, advisory
// lock, telemetry) lands here instead of every call site.
FILE   *open_file_shared(const char *path, const char *mode);

int64_t get_file_size(FILE *fp);
int64_t search_bytes(FILE *fp, int64_t file_size,
                     int64_t start, const unsigned char *pattern, int pattern_len);
int write_byte_at(FILE *fp, int64_t offset, unsigned char val);

// Transient write handle: open, patch, close — hxediter never holds write
// access when idle. Returns 0 on success, -1 on failure.
int write_byte_at_path(const char *path, int64_t offset, unsigned char val);

// Atomic read-then-write for a single byte at `offset`. Returns 0 on
// success and writes the previous byte value to *out_old_val; -1 on any
// I/O failure.
//
// The seek/read/seek/write sequence runs under a single rb+ FILE* — no
// close+reopen between read and write — so the window where a concurrent
// external writer could race us is sub-microsecond. No OS-level advisory
// lock is taken; the design deliberately permits concurrent external
// writers.
int replace_byte_at_path(const char *path, int64_t offset,
                         unsigned char new_val, unsigned char *out_old_val);

// POSIX has no OS-enforced sharing mode at open time, so nothing to
// detect — always returns false. Stub kept so callers read unchanged
// from the cross-platform era.
bool is_file_held_by_other_process(const char *path);

// Equality token folding mtime and size; -1 on error. Not portable
// across machines, but stable while the file is at rest.
int64_t get_file_mtime_token(const char *path);

#endif
