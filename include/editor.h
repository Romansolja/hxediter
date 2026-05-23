#ifndef EDITOR_H
#define EDITOR_H

#include "platform.h"

typedef struct {
    FILE       *fp;
    const char *filename;
    int64_t     file_size;
    int         is_readonly;

    UndoEntry   undo_stack[UNDO_MAX];
    int         undo_count;
    int         undo_head;
} EditorState;

#endif
