#include <stdio.h>
#include <ctype.h>

#define BYTES_PER_LINE 16

void print_hex_line(unsigned char *buf, int len, int offset)
{
    int i;

    printf("%08X  ", offset);

    for (i = 0; i < BYTES_PER_LINE; i++) {
        if (i < len) {
            printf("%02X ", buf[i]);
        } else {
            printf("   ");
        }

        if (i == 7) {
            printf(" ");
        }
    }

    printf(" |");
    for (i = 0; i < len; i++) {
        if (isprint(buf[i])) {
            printf("%c", buf[i]);
        } else {
            printf(".");
        }
    }
    printf("|\n");
}

int main(int argc, char *argv[])
{
    FILE *fp;
    unsigned char buf[BYTES_PER_LINE];
    int bytes_read;
    int offset = 0;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return 1;
    }

    fp = fopen(argv[1], "rb");
    if (fp == NULL) {
        fprintf(stderr, "Error: cannot open '%s'\n", argv[1]);
        return 1;
    }

    while ((bytes_read = fread(buf, 1, BYTES_PER_LINE, fp)) > 0) {
        print_hex_line(buf, bytes_read, offset);
        offset += bytes_read;
    }

    fclose(fp);

    return 0;
}
