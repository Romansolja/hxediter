#ifndef PLATFORM_H
#define PLATFORM_H

// MUST be defined before any system header is included.
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

// Sized for someone picking through a binary: at 16 bytes per entry the
// ring is 4 KB per open file, trivial against the ~MB cost of an ImGui
// context. The 65th-edit-drops-the-first behavior in undo.cpp still
// applies — the bound is just bigger now.
#define UNDO_MAX 256
#define SEARCH_CHUNK 4096

typedef struct {
    int64_t offset;
    unsigned char old_val;
    unsigned char new_val;
} UndoEntry;

#endif
