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

#if defined(_WIN32)
    #define fseek64(fp, off, whence) _fseeki64((fp), (__int64)(off), (whence))
    #define ftell64(fp)              ((int64_t)_ftelli64(fp))
#else
    #include <sys/types.h>
    #define fseek64(fp, off, whence) fseeko((fp), (off_t)(off), (whence))
    #define ftell64(fp)              ((int64_t)ftello(fp))
#endif

#define BYTES_PER_LINE 16
#define LINES_PER_PAGE 16
#define PAGE_SIZE (BYTES_PER_LINE * LINES_PER_PAGE)
#define MAX_SEARCH_BYTES 16
#define INPUT_BUF_SIZE 128
#define UNDO_MAX 64
#define SEARCH_CHUNK 4096

typedef struct {
    int64_t offset;
    unsigned char old_val;
    unsigned char new_val;
} UndoEntry;

#endif
