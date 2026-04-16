/* fileops.h — File I/O operations */

#ifndef FILEOPS_H
#define FILEOPS_H

#include "platform.h"

/* Opens a file with shared read+write access on Windows so that other
 * processes (Notepad, editors, build tools) can still save over it while
 * hxediter has it open. MSVC's plain fopen defaults to deny-write, which
 * silently locks the file against external saves — we don't want that.
 * On non-Windows, this is just fopen. Mode strings match fopen exactly. */
FILE   *open_file_shared(const char *path, const char *mode);

int64_t get_file_size(FILE *fp);
int64_t search_bytes(FILE *fp, int64_t file_size,
                     int64_t start, const unsigned char *pattern, int pattern_len);
int write_byte_at(FILE *fp, int64_t offset, unsigned char val);

/* Opens a transient write handle via open_file_shared, patches one byte,
 * and closes it — so hxediter never holds write access when it's idle.
 * Returns 0 on success, -1 on failure (open or write). */
int write_byte_at_path(const char *path, int64_t offset, unsigned char val);

/* Returns true if another process currently holds the file open in a way
 * that blocks exclusive access. Windows-only; always false elsewhere. */
bool is_file_held_by_other_process(const char *path);

/* Returns the file's last-modified time as a 64-bit token suitable for
 * equality comparison (mtime * 1e6 + size), or -1 on error. Not portable
 * across machines, but stable while the file is at rest on disk. */
int64_t get_file_mtime_token(const char *path);

#endif /* FILEOPS_H */
