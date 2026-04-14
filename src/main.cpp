/* hxediter — Interactive hex viewer/editor */

#include "hex_editor_core.h"
#include "display.h"
#include <cstdio>
#include <cstdlib>
#include <cctype>

static void show_page(HexEditorCore& core)
{
    auto data = core.GetPageData();
    display_page_data(data.data(), data.size(), core.GetCurrentOffset());
    print_status(core.GetCurrentOffset(), core.GetFileSize());
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return 1;
    }

    HexEditorCore core(argv[1]);

    if (core.GetFileSize() == 0) {
        printf("File is empty.\n");
        return 0;
    }

    printf("=== hxediter: %s (%" PRId64 " bytes)%s ===\n",
           core.GetFilename().c_str(), core.GetFileSize(),
           core.IsReadOnly() ? " [READ-ONLY]" : "");
    show_page(core);

    char input_buf[INPUT_BUF_SIZE];

    while (1) {
        printf("> ");

        if (fgets(input_buf, sizeof(input_buf), stdin) == NULL)
            break;

        switch (tolower((unsigned char)input_buf[0])) {

        case 'n':
        case '\n':
            if (core.PageNext()) {
                show_page(core);
            } else {
                printf("Already at the end of the file.\n");
            }
            break;

        case 'p':
            if (core.PagePrev()) {
                show_page(core);
            } else {
                printf("Already at the start of the file.\n");
            }
            break;

        case 'g': {
            uint64_t new_offset = 0;

            if (sscanf(input_buf + 1, " %" SCNx64, &new_offset) != 1) {
                printf("Usage: g <hex_offset>   (example: g 1A0)\n");
                break;
            }

            if (!core.GoToOffset(static_cast<int64_t>(new_offset))) {
                printf("Offset 0x%" PRIX64 " is out of range (file is 0x%" PRIX64 " bytes)\n",
                       new_offset, core.GetFileSize());
                break;
            }

            show_page(core);
            printf("  (Jumped to page containing offset 0x%08" PRIX64 ")\n", new_offset);
            break;
        }

        case 's': {
            std::vector<unsigned char> pattern;
            char *p = input_buf + 1;

            while (pattern.size() < MAX_SEARCH_BYTES) {
                char *end_ptr;

                while (*p == ' ' || *p == '\t')
                    p++;
                if (*p == '\0')
                    break;

                long byte_val = strtol(p, &end_ptr, 16);
                if (end_ptr == p)
                    break;
                if (byte_val < 0 || byte_val > 0xFF) {
                    printf("Byte value out of range (00-FF): %lX\n", byte_val);
                    pattern.clear();
                    break;
                }

                pattern.push_back(static_cast<unsigned char>(byte_val));
                p = end_ptr;
            }

            if (pattern.empty()) {
                printf("Usage: s <hex bytes>   (example: s 48 65 6C 6C 6F)\n");
                break;
            }

            printf("Searching for %d byte(s)...\n", (int)pattern.size());
            auto result = core.Search(pattern);

            if (result) {
                show_page(core);
                printf("  Found at offset 0x%08" PRIX64 " (%" PRId64 ")\n",
                       result->offset, result->offset);
            } else {
                printf("  Pattern not found.\n");
            }
            break;
        }

        case 'e': {
            uint64_t edit_offset = 0;
            unsigned int byte_val = 0;

            if (core.IsReadOnly()) {
                printf("Error: File opened in read-only mode.\n");
                break;
            }

            if (sscanf(input_buf + 1, " %" SCNx64 " %x", &edit_offset, &byte_val) != 2) {
                printf("Usage: e <hex_offset> <hex_byte>   (example: e 1A0 FF)\n");
                break;
            }

            if (byte_val > 0xFF) {
                printf("Byte value 0x%X is too large (must be 00-FF)\n", byte_val);
                break;
            }

            auto result = core.EditByte(static_cast<int64_t>(edit_offset),
                                        static_cast<unsigned char>(byte_val));

            if (result) {
                show_page(core);
                printf("  Changed byte at 0x%08" PRIX64 ": 0x%02X -> 0x%02X\n",
                       result->offset, result->old_val, result->new_val);
            } else {
                printf("Error: failed to edit byte at 0x%" PRIX64 "\n", edit_offset);
            }
            break;
        }

        case 'u': {
            auto result = core.Undo();

            if (result) {
                show_page(core);
                printf("  Undid byte at 0x%08" PRIX64 ": 0x%02X -> 0x%02X (%d undo(s) left)\n",
                       result->offset, result->undone_val, result->restored_val,
                       result->remaining_undos);
            } else {
                printf("Nothing to undo.\n");
            }
            break;
        }

        case 'q':
            printf("Goodbye!\n");
            return 0;

        default:
            printf("Unknown command '%c'. Commands: [N]ext [P]rev [G offset] [S hex] [E offset byte] [U]ndo [Q]uit\n",
                   input_buf[0]);
            break;
        }
    }

    return 0;
}
