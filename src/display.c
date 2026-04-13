/* display.c — Hex display output */

#include "display.h"

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
