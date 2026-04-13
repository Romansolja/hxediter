/* display.h — Hex display output */

#ifndef DISPLAY_H
#define DISPLAY_H

#include "platform.h"

void print_hex_line(const unsigned char *buf, int len, int64_t offset);
void display_page(FILE *fp, int64_t page_offset);
void print_status(int64_t page_offset, int64_t file_size);

#endif /* DISPLAY_H */
