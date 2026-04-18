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

/* Windows-only; always false elsewhere. */
bool is_file_held_by_other_process(const char *path);

/* Equality token folding mtime and size; -1 on error. Not portable
 * across machines, but stable while the file is at rest. */
int64_t get_file_mtime_token(const char *path);

#endif
