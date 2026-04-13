/* undo.h — Undo ring buffer operations */

#ifndef UNDO_H
#define UNDO_H

#include "editor.h"

void undo_push(EditorState *state, int64_t offset,
               unsigned char old_val, unsigned char new_val);
int  undo_pop(EditorState *state, UndoEntry *out);
void undo_unpop(EditorState *state);

#endif /* UNDO_H */
