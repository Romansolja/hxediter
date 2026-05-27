#ifndef EDITOR_H
#define EDITOR_H

#include "platform.h"

// POD by intent: undo.cpp pokes the ring fields by raw pointer. The read
// FILE* used to live here as a public field, but that forced every site
// touching `state_` to participate in lifetime management — it now lives
// as a FileHandle private member of HexEditorCore.
typedef struct {
    const char *filename;
    int64_t     file_size;
    int         is_readonly;

    UndoEntry   undo_stack[UNDO_MAX];
    int         undo_count;
    int         undo_head;
} EditorState;

#endif
