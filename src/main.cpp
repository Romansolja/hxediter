/* hxediter — Interactive hex viewer/editor */

#include "editor.h"
#include "display.h"
#include "fileops.h"
#include "undo.h"

int main(int argc, char *argv[])
{
    EditorState state = {};

    /* Check args and open file */
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return 1;
    }

    state.filename = argv[1];

    /* Try read+write first; fall back to read-only for files we cannot modify
     * (system files, executables in use, files with read-only permissions). */
    state.fp = fopen(state.filename, "rb+");
    if (state.fp == NULL) {
        state.fp = fopen(state.filename, "rb");
        if (state.fp == NULL) {
            fprintf(stderr, "Error: cannot open '%s'\n", state.filename);
            return 1;
        }
        state.is_readonly = 1;
    }

    /* Get file size */
    state.file_size = get_file_size(state.fp);
    if (state.file_size < 0) {
        fprintf(stderr, "Error: cannot determine file size\n");
        fclose(state.fp);
        return 1;
    }
    if (state.file_size == 0) {
        printf("File is empty.\n");
        fclose(state.fp);
        return 0;
    }

    /* Show first page */
    printf("=== hxediter: %s (%" PRId64 " bytes)%s ===\n",
           state.filename, state.file_size,
           state.is_readonly ? " [READ-ONLY]" : "");
    display_page(state.fp, state.page_offset);
    print_status(state.page_offset, state.file_size);

    /* Command loop */
    while (1) {
        printf("> ");

        if (fgets(state.input_buf, sizeof(state.input_buf), stdin) == NULL)
            break;

        switch (tolower((unsigned char)state.input_buf[0])) {

        case 'n':
        case '\n':
            if (state.page_offset + PAGE_SIZE < state.file_size) {
                state.page_offset += PAGE_SIZE;
                display_page(state.fp, state.page_offset);
                print_status(state.page_offset, state.file_size);
            } else {
                printf("Already at the end of the file.\n");
            }
            break;

        case 'p':
            if (state.page_offset >= PAGE_SIZE) {
                state.page_offset -= PAGE_SIZE;
            } else if (state.page_offset > 0) {
                state.page_offset = 0;
            } else {
                printf("Already at the start of the file.\n");
                break;
            }
            display_page(state.fp, state.page_offset);
            print_status(state.page_offset, state.file_size);
            break;

        case 'g': {
            uint64_t new_offset = 0;

            if (sscanf(state.input_buf + 1, " %" SCNx64, &new_offset) != 1) {
                printf("Usage: g <hex_offset>   (example: g 1A0)\n");
                break;
            }

            if (new_offset >= (uint64_t)state.file_size) {
                printf("Offset 0x%" PRIX64 " is out of range (file is 0x%" PRIX64 " bytes)\n",
                       new_offset, state.file_size);
                break;
            }

            state.page_offset = (int64_t)((new_offset / PAGE_SIZE) * PAGE_SIZE);
            display_page(state.fp, state.page_offset);
            print_status(state.page_offset, state.file_size);
            printf("  (Jumped to page containing offset 0x%08" PRIX64 ")\n", new_offset);
            break;
        }

        case 's': {
            unsigned char pattern[MAX_SEARCH_BYTES];
            int pattern_len = 0;
            char *p = state.input_buf + 1;
            int64_t result;

            /* Parse space-separated hex bytes from input using strtol.
             * strtol sets end_ptr to the first unparsed character, so we
             * can safely advance p without needing %n. */
            while (pattern_len < MAX_SEARCH_BYTES) {
                char *end_ptr;
                long byte_val;

                /* Skip leading whitespace so end_ptr == p means "no digits" */
                while (*p == ' ' || *p == '\t')
                    p++;
                if (*p == '\0')
                    break;

                byte_val = strtol(p, &end_ptr, 16);
                if (end_ptr == p)              /* no hex digits consumed */
                    break;
                if (byte_val < 0 || byte_val > 0xFF) {
                    printf("Byte value out of range (00-FF): %lX\n", byte_val);
                    pattern_len = 0;           /* abort this search */
                    break;
                }

                pattern[pattern_len++] = (unsigned char)byte_val;
                p = end_ptr;
            }

            if (pattern_len == 0) {
                printf("Usage: s <hex bytes>   (example: s 48 65 6C 6C 6F)\n");
                break;
            }

            printf("Searching for %d byte(s)...\n", pattern_len);
            result = search_bytes(state.fp, state.file_size,
                                  state.page_offset, pattern, pattern_len);

            /* Wrap around to beginning if not found */
            if (result == -1 && state.page_offset > 0) {
                printf("  (Wrapping around to start of file...)\n");
                result = search_bytes(state.fp, state.file_size, 0, pattern, pattern_len);
            }

            if (result != -1) {
                state.page_offset = (result / PAGE_SIZE) * PAGE_SIZE;
                display_page(state.fp, state.page_offset);
                print_status(state.page_offset, state.file_size);
                printf("  Found at offset 0x%08" PRIX64 " (%" PRId64 ")\n", result, result);
            } else {
                printf("  Pattern not found.\n");
            }
            break;
        }

        case 'e': {
            uint64_t edit_offset = 0;
            unsigned int byte_val = 0;
            int old_ch;
            unsigned char old_val;

            if (state.is_readonly) {
                printf("Error: File opened in read-only mode.\n");
                break;
            }

            if (sscanf(state.input_buf + 1, " %" SCNx64 " %x", &edit_offset, &byte_val) != 2) {
                printf("Usage: e <hex_offset> <hex_byte>   (example: e 1A0 FF)\n");
                break;
            }

            if (edit_offset >= (uint64_t)state.file_size) {
                printf("Offset 0x%" PRIX64 " is out of range (file is 0x%" PRIX64 " bytes)\n",
                       edit_offset, state.file_size);
                break;
            }

            if (byte_val > 0xFF) {
                printf("Byte value 0x%X is too large (must be 00-FF)\n", byte_val);
                break;
            }

            /* Read the existing byte so we can save it for undo. */
            if (fseek64(state.fp, (int64_t)edit_offset, SEEK_SET) != 0 ||
                (old_ch = fgetc(state.fp)) == EOF) {
                printf("Error: cannot read byte at offset 0x%" PRIX64 "\n", edit_offset);
                break;
            }
            old_val = (unsigned char)old_ch;

            if (write_byte_at(state.fp, (int64_t)edit_offset, (unsigned char)byte_val) != 0) {
                printf("Error: failed to write byte at 0x%" PRIX64 "\n", edit_offset);
                break;
            }

            undo_push(&state, (int64_t)edit_offset, old_val, (unsigned char)byte_val);

            state.page_offset = (int64_t)((edit_offset / PAGE_SIZE) * PAGE_SIZE);
            display_page(state.fp, state.page_offset);
            print_status(state.page_offset, state.file_size);
            printf("  Changed byte at 0x%08" PRIX64 ": 0x%02X -> 0x%02X\n",
                   edit_offset, old_val, (unsigned char)byte_val);
            break;
        }

        case 'u': {
            UndoEntry entry;

            if (!undo_pop(&state, &entry)) {
                printf("Nothing to undo.\n");
                break;
            }

            if (write_byte_at(state.fp, entry.offset, entry.old_val) != 0) {
                printf("Error: failed to write byte at 0x%" PRIX64 "\n", entry.offset);
                /* Roll head forward again so the failed undo stays on the stack */
                undo_unpop(&state);
                break;
            }

            state.page_offset = (entry.offset / PAGE_SIZE) * PAGE_SIZE;
            display_page(state.fp, state.page_offset);
            print_status(state.page_offset, state.file_size);
            printf("  Undid byte at 0x%08" PRIX64 ": 0x%02X -> 0x%02X (%d undo(s) left)\n",
                   entry.offset, entry.new_val, entry.old_val, state.undo_count);
            break;
        }

        case 'q':
            printf("Goodbye!\n");
            fclose(state.fp);
            return 0;

        default:
            printf("Unknown command '%c'. Commands: [N]ext [P]rev [G offset] [S hex] [E offset byte] [U]ndo [Q]uit\n",
                   state.input_buf[0]);
            break;
        }
    }

    fclose(state.fp);
    return 0;
}
