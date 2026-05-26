#ifndef FILEOPS_H
#define FILEOPS_H

#include "platform.h"

FILE   *open_file_shared(const char *path, const char *mode);

int64_t get_file_size(FILE *fp);
int64_t search_bytes(FILE *fp, int64_t file_size,
                     int64_t start, const unsigned char *pattern, int pattern_len);
int write_byte_at(FILE *fp, int64_t offset, unsigned char val);

// Open, patch, close — hxediter never holds write access when idle. 0 on success, -1 otherwise.
int write_byte_at_path(const char *path, int64_t offset, unsigned char val);

// Atomic byte read+write under one rb+ FILE*. Writes the previous byte to *out_old_val.
// No OS-level lock — concurrent external writers are deliberately allowed.
int replace_byte_at_path(const char *path, int64_t offset,
                         unsigned char new_val, unsigned char *out_old_val);

// Always false on POSIX — no OS-enforced sharing mode at open time.
bool is_file_held_by_other_process(const char *path);

// mtime+size equality token. -1 on error. Not portable across machines.
int64_t get_file_mtime_token(const char *path);

#endif
