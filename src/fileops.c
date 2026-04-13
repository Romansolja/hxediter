/* fileops.c — File I/O operations */

#include "fileops.h"

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
