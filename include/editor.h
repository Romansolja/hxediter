/* editor.h — Central editor state */

#ifndef EDITOR_H
#define EDITOR_H

#include "platform.h"

typedef struct {
    FILE       *fp;
    const char *filename;
    int64_t     file_size;
    int64_t     page_offset;
    int         is_readonly;

    /* Undo ring buffer */
    UndoEntry   undo_stack[UNDO_MAX];
    int         undo_count;
    int         undo_head;

    /* Input buffer for command loop */
    char        input_buf[INPUT_BUF_SIZE];
} EditorState;

#endif /* EDITOR_H */
