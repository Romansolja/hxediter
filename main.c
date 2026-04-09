#include <string.h>
#include <ctype.h>

#define BYTES_PER_LINE 16
#define LINES_PER_PAGE 16
#define PAGE_SIZE (BYTES_PER_LINE * LINES_PER_PAGE)  /* 256 bytes per page */
#define MAX_SEARCH_BYTES 16
#define INPUT_BUF_SIZE 128

/* Returns file size in bytes, or -1 on error */
long get_file_size(FILE *fp)
{
    long size;

    if (fseek(fp, 0, SEEK_END) != 0)
        return -1;

    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);  /* Rewind to start */

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

/* Displays one page (256 bytes) starting at page_offset */
void display_page(unsigned char *file_buf, long file_size, long page_offset)
{
    long bytes_remaining = file_size - page_offset;
    long bytes_to_show;
    long i;
    int line_len;

    if (bytes_remaining <= 0) {
        printf("(End of file)\n");
        return;
    }

    bytes_to_show = (bytes_remaining < PAGE_SIZE) ? bytes_remaining : PAGE_SIZE;

    printf("\n");
    for (i = 0; i < bytes_to_show; i += BYTES_PER_LINE) {
        line_len = BYTES_PER_LINE;
        if (bytes_to_show - i < BYTES_PER_LINE)
            line_len = (int)(bytes_to_show - i);

        print_hex_line(file_buf + page_offset + i, line_len, page_offset + i);
    }
}

/* Shows current position & available commands */
void print_status(long page_offset, long file_size)
{
    long current_page = (page_offset / PAGE_SIZE) + 1;
    long total_pages  = (file_size + PAGE_SIZE - 1) / PAGE_SIZE;

    printf("--------------------------------------------------------------\n"
           "  Offset: 0x%08lX (%ld)  |  Page %ld of %ld  |  Size: %ld bytes\n"
           "  [N]ext  [P]rev  [G offset]  [S hex bytes]  [Q]uit\n"
           "--------------------------------------------------------------\n",
           page_offset, page_offset, current_page, total_pages, file_size);
}

/* Searches for a byte pattern. Returns offset of match, or -1 IF not found */
long search_bytes(unsigned char *file_buf, long file_size,
                  long start, unsigned char *pattern, int pattern_len)
{
    long i;

    if (pattern_len <= 0 || start + pattern_len > file_size)
        return -1;

    for (i = start; i <= file_size - pattern_len; i++) {
        if (memcmp(file_buf + i, pattern, pattern_len) == 0)
            return i;
    }

    return -1;
}

int main(int argc, char *argv[])
{
    FILE *fp;
    unsigned char *file_buf;
    long file_size;
    long page_offset = 0;
    char input_buf[INPUT_BUF_SIZE];

    /* Check args & open file */
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return 1;
    }

    fp = fopen(argv[1], "rb");
    if (fp == NULL) {
        fprintf(stderr, "Error: cannot open '%s'\n", argv[1]);
        return 1;
    }

    /* File size */
    file_size = get_file_size(fp);
    if (file_size <= 0) {
        fprintf(stderr, "Error: file is empty or cannot determine size\n");
        fclose(fp);
        return 1;
    }

    /* Allocate buffer & load entire file into memory */
    file_buf = (unsigned char *)malloc(file_size);
    if (file_buf == NULL) {
        fprintf(stderr, "Error: not enough memory to load file (%ld bytes)\n", file_size);
        fclose(fp);
        return 1;
    }

    if (fread(file_buf, 1, file_size, fp) != (size_t)file_size) {
        fprintf(stderr, "Error: could not read entire file\n");
        free(file_buf);
        fclose(fp);
        return 1;
    }

    fclose(fp);

    /* First page */
    printf("=== hxediter: %s (%ld bytes) ===\n", argv[1], file_size);
    display_page(file_buf, file_size, page_offset);
    print_status(page_offset, file_size);

    /* Command loop */
    while (1) {
        printf("> ");

        if (fgets(input_buf, sizeof(input_buf), stdin) == NULL)
            break;

        switch (tolower(input_buf[0])) {

        case 'n':
        case '\n':
            if (page_offset + PAGE_SIZE < file_size) {
                page_offset += PAGE_SIZE;
                display_page(file_buf, file_size, page_offset);
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
            display_page(file_buf, file_size, page_offset);
            print_status(page_offset, file_size);
            break;

        case 'g': {
            long new_offset = 0;

            if (sscanf(input_buf + 2, "%lx", &new_offset) != 1) {
                printf("Usage: g <hex_offset>   (example: g 1A0)\n");
                break;
            }

            if (new_offset < 0 || new_offset >= file_size) {
                printf("Offset 0x%lX is out of range (file is 0x%lX bytes)\n",
                       new_offset, file_size);
                break;
            }

            page_offset = (new_offset / PAGE_SIZE) * PAGE_SIZE;
            display_page(file_buf, file_size, page_offset);
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

            /* Parse space separated hex bytes from input */
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
            result = search_bytes(file_buf, file_size,
                                  page_offset + 1, pattern, pattern_len);

            /* Wrap around to beginning IF not found */
            if (result == -1 && page_offset > 0) {
                printf("  (Wrapping around to start of file...)\n");
                result = search_bytes(file_buf, file_size, 0, pattern, pattern_len);
            }

            if (result != -1) {
                page_offset = (result / PAGE_SIZE) * PAGE_SIZE;
                display_page(file_buf, file_size, page_offset);
                print_status(page_offset, file_size);
                printf("  Found at offset 0x%08lX (%ld)\n", result, result);
            } else {
                printf("  Pattern not found.\n");
            }
            break;
        }

        case 'q':
            printf("Goodbye!\n");
            free(file_buf);
            return 0;

        default:
            printf("Unknown command '%c'. Commands: [N]ext [P]rev [G offset] [S hex] [Q]uit\n",
                   input_buf[0]);
            break;
        }
    }

    free(file_buf);
    return 0;
}
