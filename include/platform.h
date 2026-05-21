#ifndef PLATFORM_H
#define PLATFORM_H

/* MUST be defined before any system header is included. */
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <inttypes.h>

#include <sys/types.h>

#define INPUT_BUF_SIZE 128
#define UNDO_MAX 64
#define SEARCH_CHUNK 4096

typedef struct {
    int64_t offset;
    unsigned char old_val;
    unsigned char new_val;
} UndoEntry;

#endif
