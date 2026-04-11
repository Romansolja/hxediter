/* hxediter- Interactive hex viewer */

/* Ensure 64-bit file offsets on POSIX systems.
 * MUST be defined before any system header is included. */
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
#define PAGE_SIZE (BYTES_PER_LINE * LINES_PER_PAGE)  /* 256 bytes per page */
#define MAX_SEARCH_BYTES 16
#define INPUT_BUF_SIZE 128
#define UNDO_MAX 64
#define SEARCH_CHUNK 4096

typedef struct {
    int64_t offset;
    unsigned char old_val;
    unsigned char new_val;
} UndoEntry;

/* Returns file size in bytes, or -1 on error */
int64_t get_file_size(FILE *fp)
{
    int64_t size;

    if (fseek64(fp, 0, SEEK_END) != 0)
        return -1;

    size = ftell64(fp);

    if (fseek64(fp, 0, SEEK_SET) != 0)
        return -1;

    return size;
}

/* Prints one row: offset + 16 hex bytes + ASCII */
void print_hex_line(const unsigned char *buf, int len, int64_t offset)
{
    int i;

    printf("%08" PRIX64 "  ", offset);

    for (i = 0; i < BYTES_PER_LINE; i++) {
        if (i < len)
            printf("%02X ", buf[i]);
        else
            printf("   ");

        if (i == 7)
            printf(" ");
    }

    printf(" |");
    for (i = 0; i < len; i++) {
        if (isprint(buf[i]))
            printf("%c", buf[i]);
        else
            printf(".");
    }
    printf("|\n");
}

/* Displays one page (up to 256 bytes) starting at page_offset.
 * Reads directly from the file instead of holding it all in memory. */
void display_page(FILE *fp, int64_t page_offset)
{
    unsigned char page_buf[PAGE_SIZE];
    size_t got;
    int64_t bytes_to_show;
    int64_t i;
    int line_len;

    if (fseek64(fp, page_offset, SEEK_SET) != 0) {
        printf("(Seek error)\n");
        return;
    }

    got = fread(page_buf, 1, PAGE_SIZE, fp);
    if (got == 0) {
        printf("(End of file)\n");
        return;
    }

    bytes_to_show = (int64_t)got;

    printf("\n");
    for (i = 0; i < bytes_to_show; i += BYTES_PER_LINE) {
        line_len = BYTES_PER_LINE;
        if (bytes_to_show - i < BYTES_PER_LINE)
            line_len = (int)(bytes_to_show - i);

        print_hex_line(page_buf + i, line_len, page_offset + i);
    }
}

/* Shows current position and available commands */
void print_status(int64_t page_offset, int64_t file_size)
{
    int64_t current_page = (page_offset / PAGE_SIZE) + 1;
    int64_t total_pages  = (file_size + PAGE_SIZE - 1) / PAGE_SIZE;

    printf("--------------------------------------------------------------\n"
           "  Offset: 0x%08" PRIX64 " (%" PRId64 ")  |  Page %" PRId64 " of %" PRId64 "  |  Size: %" PRId64 " bytes\n"
           "  [N]ext  [P]rev  [G offset]  [S hex bytes]  [E offset byte]  [U]ndo  [Q]uit\n"
           "--------------------------------------------------------------\n",
           page_offset, page_offset, current_page, total_pages, file_size);
}

/* Searches for a byte pattern by streaming the file in overlapping chunks.
 * Returns offset of match, or -1 if not found. */
int64_t search_bytes(FILE *fp, int64_t file_size,
                     int64_t start, const unsigned char *pattern, int pattern_len)
{
    unsigned char chunk[SEARCH_CHUNK];
    int64_t pos;

    if (pattern_len <= 0 || start < 0 || start + pattern_len > file_size)
        return -1;

    pos = start;
    while (pos + pattern_len <= file_size) {
        size_t got;
        unsigned char *search_ptr;
        int remaining;

        if (fseek64(fp, pos, SEEK_SET) != 0)
            return -1;

        got = fread(chunk, 1, SEARCH_CHUNK, fp);
        if (got < (size_t)pattern_len)
            break;  /* Not enough bytes left for any possible match */

        /* Scan this chunk: memchr for the first byte, memcmp to confirm. */
        search_ptr = chunk;
        remaining = (int)got;
        while (remaining >= pattern_len) {
            unsigned char *match = memchr(search_ptr, pattern[0],
                                          remaining - pattern_len + 1);
            if (!match)
                break;

            if (memcmp(match, pattern, pattern_len) == 0)
                return pos + (int64_t)(match - chunk);

            search_ptr = match + 1;
            remaining = (int)got - (int)(search_ptr - chunk);
        }

        /* Advance, overlapping by (pattern_len - 1) so a pattern straddling
         * the chunk boundary is still caught on the next iteration. */
        pos += (int64_t)got - (pattern_len - 1);
    }

    return -1;
}

/* Patches a single byte at 'offset' in the open file. Returns 0 on success,
 * -1 on failure. fflush forces stdio to surface any deferred write error
 * here, instead of letting it appear later as a confusing seek failure. */
int write_byte_at(FILE *fp, int64_t offset, unsigned char val)
{
    if (fseek64(fp, offset, SEEK_SET) != 0) return -1;
    if (fputc((int)val, fp) == EOF)         return -1;
    if (fflush(fp) != 0)                    return -1;
    return 0;
}

int main(int argc, char *argv[])
{
    FILE *fp;
    int64_t file_size;
    int64_t page_offset = 0;
    char input_buf[INPUT_BUF_SIZE];
    UndoEntry undo_stack[UNDO_MAX];
    int undo_count = 0;
    int undo_head = 0;   /* ring buffer: index where the next edit will be written */
    int is_readonly = 0;

    /* Check args and open file */
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return 1;
    }

    /* Try read+write first; fall back to read-only for files we cannot modify
     * (system files, executables in use, files with read-only permissions). */
    fp = fopen(argv[1], "rb+");
    if (fp == NULL) {
        fp = fopen(argv[1], "rb");
        if (fp == NULL) {
            fprintf(stderr, "Error: cannot open '%s'\n", argv[1]);
            return 1;
        }
        is_readonly = 1;
    }

    /* Get file size */
    file_size = get_file_size(fp);
    if (file_size < 0) {
        fprintf(stderr, "Error: cannot determine file size\n");
        fclose(fp);
        return 1;
    }
    if (file_size == 0) {
        printf("File is empty.\n");
        fclose(fp);
        return 0;
    }

    /* Show first page */
    printf("=== hxediter: %s (%" PRId64 " bytes)%s ===\n",
           argv[1], file_size, is_readonly ? " [READ-ONLY]" : "");
    display_page(fp, page_offset);
    print_status(page_offset, file_size);

    /* Command loop */
    while (1) {
        printf("> ");

        if (fgets(input_buf, sizeof(input_buf), stdin) == NULL)
            break;

        switch (tolower((unsigned char)input_buf[0])) {

        case 'n':
        case '\n':
            if (page_offset + PAGE_SIZE < file_size) {
                page_offset += PAGE_SIZE;
                display_page(fp, page_offset);
                print_status(page_offset, file_size);
            } else {
                printf("Already at the end of the file.\n");
            }
            break;

        case 'p':
            if (page_offset >= PAGE_SIZE) {
                page_offset -= PAGE_SIZE;
            } else if (page_offset > 0) {
                page_offset = 0;
            } else {
                printf("Already at the start of the file.\n");
                break;
            }
            display_page(fp, page_offset);
            print_status(page_offset, file_size);
            break;

        case 'g': {
            uint64_t new_offset = 0;

            if (sscanf(input_buf + 1, " %" SCNx64, &new_offset) != 1) {
                printf("Usage: g <hex_offset>   (example: g 1A0)\n");
                break;
            }

            if (new_offset >= (uint64_t)file_size) {
                printf("Offset 0x%" PRIX64 " is out of range (file is 0x%" PRIX64 " bytes)\n",
                       new_offset, file_size);
                break;
            }

            page_offset = (int64_t)((new_offset / PAGE_SIZE) * PAGE_SIZE);
            display_page(fp, page_offset);
            print_status(page_offset, file_size);
            printf("  (Jumped to page containing offset 0x%08" PRIX64 ")\n", new_offset);
            break;
        }

        case 's': {
            unsigned char pattern[MAX_SEARCH_BYTES];
            int pattern_len = 0;
            char *p = input_buf + 1;
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
            result = search_bytes(fp, file_size,
                                  page_offset + 1, pattern, pattern_len);

            /* Wrap around to beginning if not found */
            if (result == -1 && page_offset > 0) {
                printf("  (Wrapping around to start of file...)\n");
                result = search_bytes(fp, file_size, 0, pattern, pattern_len);
            }

            if (result != -1) {
                page_offset = (result / PAGE_SIZE) * PAGE_SIZE;
                display_page(fp, page_offset);
                print_status(page_offset, file_size);
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

            if (is_readonly) {
                printf("Error: File opened in read-only mode.\n");
                break;
            }

            if (sscanf(input_buf + 1, " %" SCNx64 " %x", &edit_offset, &byte_val) != 2) {
                printf("Usage: e <hex_offset> <hex_byte>   (example: e 1A0 FF)\n");
                break;
            }

            if (edit_offset >= (uint64_t)file_size) {
                printf("Offset 0x%" PRIX64 " is out of range (file is 0x%" PRIX64 " bytes)\n",
                       edit_offset, file_size);
                break;
            }

            if (byte_val > 0xFF) {
                printf("Byte value 0x%X is too large (must be 00-FF)\n", byte_val);
                break;
            }

            /* Read the existing byte so we can save it for undo. The fseek
             * here also serves as the read->write fence required by C
             * before write_byte_at writes the new value. */
            if (fseek64(fp, (int64_t)edit_offset, SEEK_SET) != 0 ||
                (old_ch = fgetc(fp)) == EOF) {
                printf("Error: cannot read byte at offset 0x%" PRIX64 "\n", edit_offset);
                break;
            }
            old_val = (unsigned char)old_ch;

            if (write_byte_at(fp, (int64_t)edit_offset, (unsigned char)byte_val) != 0) {
                printf("Error: failed to write byte at 0x%" PRIX64 "\n", edit_offset);
                break;
            }

            /* Ring buffer push: write at undo_head, advance with wraparound.
             * If the buffer is already full, this silently overwrites the
             * oldest entry — exactly what we want, so the user always keeps
             * the most recent UNDO_MAX edits undoable. */
            undo_stack[undo_head].offset = (int64_t)edit_offset;
            undo_stack[undo_head].old_val = old_val;
            undo_stack[undo_head].new_val = (unsigned char)byte_val;
            undo_head = (undo_head + 1) % UNDO_MAX;
            if (undo_count < UNDO_MAX)
                undo_count++;

            page_offset = (int64_t)((edit_offset / PAGE_SIZE) * PAGE_SIZE);
            display_page(fp, page_offset);
            print_status(page_offset, file_size);
            printf("  Changed byte at 0x%08" PRIX64 ": 0x%02X -> 0x%02X\n",
                   edit_offset, old_val, (unsigned char)byte_val);
            break;
        }

        case 'u': {
            UndoEntry *entry;

            if (undo_count == 0) {
                printf("Nothing to undo.\n");
                break;
            }

            /* Ring buffer pop: step undo_head back one slot (with wrap), and
             * read the entry there. We add UNDO_MAX before the modulo so the
             * intermediate value is never negative — C's % on negatives is
             * implementation-defined and best avoided. */
            undo_head = (undo_head - 1 + UNDO_MAX) % UNDO_MAX;
            entry = &undo_stack[undo_head];

            if (write_byte_at(fp, entry->offset, entry->old_val) != 0) {
                printf("Error: failed to write byte at 0x%" PRIX64 "\n", entry->offset);
                /* Roll head forward again so the failed undo stays on the stack */
                undo_head = (undo_head + 1) % UNDO_MAX;
                break;
            }

            undo_count--;

            page_offset = (entry->offset / PAGE_SIZE) * PAGE_SIZE;
            display_page(fp, page_offset);
            print_status(page_offset, file_size);
            printf("  Undid byte at 0x%08" PRIX64 ": 0x%02X -> 0x%02X (%d undo(s) left)\n",
                   entry->offset, entry->new_val, entry->old_val, undo_count);
            break;
        }

        case 'q':
            printf("Goodbye!\n");
            fclose(fp);
            return 0;

        default:
            printf("Unknown command '%c'. Commands: [N]ext [P]rev [G offset] [S hex] [E offset byte] [U]ndo [Q]uit\n",
                   input_buf[0]);
            break;
        }
    }

    fclose(fp);
    return 0;
}
