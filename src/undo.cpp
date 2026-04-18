#include "undo.h"

/* Oldest entry is silently overwritten when the ring is full. */
void undo_push(EditorState *state, int64_t offset,
               unsigned char old_val, unsigned char new_val)
{
    state->undo_stack[state->undo_head].offset  = offset;
    state->undo_stack[state->undo_head].old_val = old_val;
    state->undo_stack[state->undo_head].new_val = new_val;
    state->undo_head = (state->undo_head + 1) % UNDO_MAX;
    if (state->undo_count < UNDO_MAX)
        state->undo_count++;
}

int undo_pop(EditorState *state, UndoEntry *out)
{
    if (state->undo_count == 0)
        return 0;

    state->undo_head = (state->undo_head - 1 + UNDO_MAX) % UNDO_MAX;
    *out = state->undo_stack[state->undo_head];
    state->undo_count--;
    return 1;
}

/* Roll back a pop that couldn't complete (e.g. write failure).
 * Must be called immediately after the failed undo_pop. */
void undo_unpop(EditorState *state)
{
    state->undo_head = (state->undo_head + 1) % UNDO_MAX;
    if (state->undo_count < UNDO_MAX)
        state->undo_count++;
}
