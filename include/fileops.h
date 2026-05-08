#ifndef FILEOPS_H
#define FILEOPS_H

#include "platform.h"

/* Shared read+write access on Windows so external tools can still save
 * over the file while hxediter has it open. MSVC's plain fopen defaults
 * to deny-write, which silently locks the file against external saves. */
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
 * On Windows, holds an exclusive byte-range lock (LockFileEx on a 1-byte
 * range) for the read+write so no concurrent external writer can change
 * the byte between the read and the write — closes the TOCTOU window
 * where the undo stack would otherwise capture a stale "old" value.
 *
 * On POSIX, uses a single rb+ FILE* handle so the seek/read/seek/write
 * sequence runs without an intervening close+reopen, but does not take
 * an OS-level advisory lock (POSIX flock/fcntl don't compose well with
 * stdio FILE*; the residual window is sub-microsecond and the design
 * deliberately supports concurrent external writers).
 *
 * Returns -1 if the file cannot be opened, the lock cannot be acquired,
 * or any I/O step fails. */
int replace_byte_at_path(const char *path, int64_t offset,
                         unsigned char new_val, unsigned char *out_old_val);

/* Windows-only; always false elsewhere. */
bool is_file_held_by_other_process(const char *path);

/* Equality token folding mtime and size; -1 on error. Not portable
 * across machines, but stable while the file is at rest. */
int64_t get_file_mtime_token(const char *path);

#endif
