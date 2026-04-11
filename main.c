/* hxediter- Interactive hex viewer */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define BYTES_PER_LINE 16
#define LINES_PER_PAGE 16
#define PAGE_SIZE (BYTES_PER_LINE * LINES_PER_PAGE)  /* 256 bytes per page */
#define MAX_SEARCH_BYTES 16
#define INPUT_BUF_SIZE 128
#define UNDO_MAX 64
#define SEARCH_CHUNK 4096

typedef struct {
    long offset;
    unsigned char old_val;
    unsigned char new_val;
} UndoEntry;

/* Returns file size in bytes, or -1 on error */
long get_file_size(FILE *fp)
{
    long size;

    if (fseek(fp, 0, SEEK_END) != 0)
        return -1;

    size = ftell(fp);

    if (fseek(fp, 0, SEEK_SET) != 0)
        return -1;

    return size;
}

/* Prints one row: offset + 16 hex bytes + ASCII */
void print_hex_line(unsigned char *buf, int len, long offset)
{
    int i;

    printf("%08lX  ", offset);

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
void display_page(FILE *fp, long page_offset)
{
    unsigned char page_buf[PAGE_SIZE];
    size_t got;
    long bytes_to_show;
    long i;
    int line_len;

    if (fseek(fp, page_offset, SEEK_SET) != 0) {
        printf("(Seek error)\n");
        return;
    }

    got = fread(page_buf, 1, PAGE_SIZE, fp);
    if (got == 0) {
        printf("(End of file)\n");
        return;
    }

    bytes_to_show = (long)got;

    printf("\n");
    for (i = 0; i < bytes_to_show; i += BYTES_PER_LINE) {
        line_len = BYTES_PER_LINE;
        if (bytes_to_show - i < BYTES_PER_LINE)
            line_len = (int)(bytes_to_show - i);

        print_hex_line(page_buf + i, line_len, page_offset + i);
    }
}

/* Shows current position and available commands */
void print_status(long page_offset, long file_size)
{
    long current_page = (page_offset / PAGE_SIZE) + 1;
    long total_pages  = (file_size + PAGE_SIZE - 1) / PAGE_SIZE;

    printf("--------------------------------------------------------------\n"
           "  Offset: 0x%08lX (%ld)  |  Page %ld of %ld  |  Size: %ld bytes\n"
           "  [N]ext  [P]rev  [G offset]  [S hex bytes]  [E offset byte]  [U]ndo  [Q]uit\n"
           "--------------------------------------------------------------\n",
           page_offset, page_offset, current_page, total_pages, file_size);
}

/* Searches for a byte pattern by streaming the file in overlapping chunks.
 * Returns offset of match, or -1 if not found. */
long search_bytes(FILE *fp, long file_size,
                  long start, unsigned char *pattern, int pattern_len)
{
    unsigned char chunk[SEARCH_CHUNK];
    long pos;

    if (pattern_len <= 0 || start < 0 || start + pattern_len > file_size)
        return -1;

    pos = start;
    while (pos + pattern_len <= file_size) {
        size_t got;
        unsigned char *search_ptr;
        int remaining;

        if (fseek(fp, pos, SEEK_SET) != 0)
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
                return pos + (long)(match - chunk);

            search_ptr = match + 1;
            remaining = (int)got - (int)(search_ptr - chunk);
        }

        /* Advance, overlapping by (pattern_len - 1) so a pattern straddling
         * the chunk boundary is still caught on the next iteration. */
        pos += (long)got - (pattern_len - 1);
    }

    return -1;
}

/* Patches a single byte at 'offset' in the open file. Returns 0 on success,
 * -1 on failure. fflush forces stdio to surface any deferred write error
 * here, instead of letting it appear later as a confusing seek failure. */
int write_byte_at(FILE *fp, long offset, unsigned char val)
{
    if (fseek(fp, offset, SEEK_SET) != 0) return -1;
    if (fputc((int)val, fp) == EOF)      return -1;
    if (fflush(fp) != 0)                 return -1;
    return 0;
}

int main(int argc, char *argv[])
{
    FILE *fp;
    long file_size;
    long page_offset = 0;
    char input_buf[INPUT_BUF_SIZE];
    UndoEntry undo_stack[UNDO_MAX];
    int undo_count = 0;
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
    printf("=== hxediter: %s (%ld bytes)%s ===\n",
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
            long new_offset = 0;

            if (sscanf(input_buf + 1, " %lx", &new_offset) != 1) {
                printf("Usage: g <hex_offset>   (example: g 1A0)\n");
                break;
            }

            if (new_offset < 0 || new_offset >= file_size) {
                printf("Offset 0x%lX is out of range (file is 0x%lX bytes)\n",
                       new_offset, file_size);
                break;
            }

            page_offset = (new_offset / PAGE_SIZE) * PAGE_SIZE;
            display_page(fp, page_offset);
            print_status(page_offset, file_size);
            printf("  (Jumped to page containing offset 0x%08lX)\n", new_offset);
            break;
        }

        case 's': {
            unsigned char pattern[MAX_SEARCH_BYTES];
            int pattern_len = 0;
            char *p = input_buf + 1;
            unsigned int byte_val;
            int chars_consumed;
            long result;

            /* Parse space-separated hex bytes from input */
            while (pattern_len < MAX_SEARCH_BYTES) {
                if (sscanf(p, " %x%n", &byte_val, &chars_consumed) != 1)
                    break;
                pattern[pattern_len] = (unsigned char)byte_val;
                pattern_len++;
                p += chars_consumed;
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
                printf("  Found at offset 0x%08lX (%ld)\n", result, result);
            } else {
                printf("  Pattern not found.\n");
            }
            break;
        }

        case 'e': {
            long edit_offset = 0;
            unsigned int byte_val = 0;
            int old_ch;
            unsigned char old_val;

            if (is_readonly) {
                printf("Error: File opened in read-only mode.\n");
                break;
            }

            if (sscanf(input_buf + 1, " %lx %x", &edit_offset, &byte_val) != 2) {
                printf("Usage: e <hex_offset> <hex_byte>   (example: e 1A0 FF)\n");
                break;
            }

            if (edit_offset < 0 || edit_offset >= file_size) {
                printf("Offset 0x%lX is out of range (file is 0x%lX bytes)\n",
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
            if (fseek(fp, edit_offset, SEEK_SET) != 0 ||
                (old_ch = fgetc(fp)) == EOF) {
                printf("Error: cannot read byte at offset 0x%lX\n", edit_offset);
                break;
            }
            old_val = (unsigned char)old_ch;

            if (write_byte_at(fp, edit_offset, (unsigned char)byte_val) != 0) {
                printf("Error: failed to write byte at 0x%lX\n", edit_offset);
                break;
            }

            if (undo_count < UNDO_MAX) {
                undo_stack[undo_count].offset = edit_offset;
                undo_stack[undo_count].old_val = old_val;
                undo_stack[undo_count].new_val = (unsigned char)byte_val;
                undo_count++;
            } else {
                printf("  Warning: undo history full, oldest edit cannot be undone\n");
            }

            page_offset = (edit_offset / PAGE_SIZE) * PAGE_SIZE;
            display_page(fp, page_offset);
            print_status(page_offset, file_size);
            printf("  Changed byte at 0x%08lX: 0x%02X -> 0x%02X\n",
                   edit_offset, old_val, (unsigned char)byte_val);
            break;
        }

        case 'u': {
            UndoEntry *entry;

            if (undo_count == 0) {
                printf("Nothing to undo.\n");
                break;
            }

            entry = &undo_stack[undo_count - 1];

            if (write_byte_at(fp, entry->offset, entry->old_val) != 0) {
                printf("Error: failed to write byte at 0x%lX\n", entry->offset);
                break;
            }

            undo_count--;

            page_offset = (entry->offset / PAGE_SIZE) * PAGE_SIZE;
            display_page(fp, page_offset);
            print_status(page_offset, file_size);
            printf("  Undid byte at 0x%08lX: 0x%02X -> 0x%02X (%d undo(s) left)\n",
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
