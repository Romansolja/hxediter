/* fileops.h — File I/O operations */

#ifndef FILEOPS_H
#define FILEOPS_H

#include "platform.h"

int64_t get_file_size(FILE *fp);
int64_t search_bytes(FILE *fp, int64_t file_size,
                     int64_t start, const unsigned char *pattern, int pattern_len);
int write_byte_at(FILE *fp, int64_t offset, unsigned char val);

#endif /* FILEOPS_H */
