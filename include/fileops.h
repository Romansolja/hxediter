#ifndef FILEOPS_H
#define FILEOPS_H

#include "platform.h"

/* Thin wrapper around fopen — kept as a single chokepoint so any future
 * per-open policy (umask tweak, advisory lock, telemetry) lands in one
 * place instead of every call site. */
FILE   *open_file_shared(const char *path, const char *mode);

int64_t get_file_size(FILE *fp);
int64_t search_bytes(FILE *fp, int64_t file_size,
                     int64_t start, const unsigned char *pattern, int pattern_len);
int write_byte_at(FILE *fp, int64_t offset, unsigned char val);

/* Transient write handle: open, patch, close — so hxediter never holds
 * write access when idle. Returns 0 on success, -1 on failure. */
int write_byte_at_path(const char *path, int64_t offset, unsigned char val);

/* Atomic read-then-write for a single byte at `offset`.
 *
 * Returns 0 on success and writes the previous byte value to *out_old_val.
 * The seek/read/seek/write sequence runs under a single rb+ FILE* handle
 * — no close+reopen between the read and the write — so the residual
 * window where a concurrent external writer could change the byte after
 * the read is sub-microsecond. No OS-level advisory lock is taken; the
 * design deliberately supports concurrent external writers.
 *
 * Returns -1 if the file cannot be opened or any I/O step fails. */
int replace_byte_at_path(const char *path, int64_t offset,
                         unsigned char new_val, unsigned char *out_old_val);

/* POSIX has no notion of an OS-enforced sharing mode at open time, so
 * there's nothing for us to detect — always returns false. Kept as a
 * stub so the call site in hex_editor_core.cpp reads the same as it
 * did before the cross-platform retreat. */
bool is_file_held_by_other_process(const char *path);

/* Equality token folding mtime and size; -1 on error. Not portable
 * across machines, but stable while the file is at rest. */
int64_t get_file_mtime_token(const char *path);

#endif
