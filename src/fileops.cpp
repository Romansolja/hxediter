#include "fileops.h"

#include <memory>
#include <sys/types.h>
#include <sys/stat.h>

FILE *open_file_shared(const char *path, const char *mode)
{
    return fopen(path, mode);
}

int64_t get_file_size(FILE *fp)
{
    int64_t size;

    if (fseeko(fp, 0, SEEK_END) != 0)
        return -1;

    size = ftello(fp);

    if (fseeko(fp, 0, SEEK_SET) != 0)
        return -1;

    return size;
}

int64_t search_bytes(FILE *fp, int64_t file_size,
                     int64_t start, const unsigned char *pattern, int pattern_len)
{
    unsigned char chunk[SEARCH_CHUNK];
    int64_t pos;

    // Subtract from file_size (not add to start) — INT64_MAX-adjacent starts would overflow.
    if (pattern_len <= 0 || start < 0 || start > file_size ||
        (int64_t)pattern_len > file_size - start)
        return -1;

    pos = start;
    while (pos + pattern_len <= file_size) {
        size_t got;
        unsigned char *search_ptr;
        int remaining;

        if (fseeko(fp, pos, SEEK_SET) != 0)
            return -1;

        got = fread(chunk, 1, SEARCH_CHUNK, fp);
        if (got < (size_t)pattern_len)
            break;

        search_ptr = chunk;
        remaining = (int)got;
        while (remaining >= pattern_len) {
            unsigned char *match = (unsigned char *)memchr(search_ptr, pattern[0],
                                          remaining - pattern_len + 1);
            if (!match)
                break;

            if (memcmp(match, pattern, pattern_len) == 0)
                return pos + (int64_t)(match - chunk);

            search_ptr = match + 1;
            remaining = (int)got - (int)(search_ptr - chunk);
        }

        // Overlap by pattern_len-1 so a pattern straddling chunks hits.
        pos += (int64_t)got - (pattern_len - 1);
    }

    return -1;
}

// fflush surfaces deferred write errors here, not later as a confusing seek failure.
int write_byte_at(FILE *fp, int64_t offset, unsigned char val)
{
    if (fseeko(fp, offset, SEEK_SET) != 0) return -1;
    if (fputc((int)val, fp) == EOF)         return -1;
    if (fflush(fp) != 0)                    return -1;
    return 0;
}

int write_byte_at_path(const char *path, int64_t offset, unsigned char val)
{
    FILE *wf = open_file_shared(path, "rb+");
    if (wf == NULL) return -1;
    int rc = write_byte_at(wf, offset, val);
    fclose(wf);
    return rc;
}

int replace_byte_at_path(const char *path, int64_t offset,
                         unsigned char new_val, unsigned char *out_old_val)
{
    if (out_old_val == NULL) return -1;
    std::unique_ptr<FILE, decltype(&std::fclose)>
        wf(open_file_shared(path, "rb+"), &std::fclose);
    if (!wf) return -1;

    if (fseeko(wf.get(), offset, SEEK_SET) != 0) return -1;
    int old_ch = fgetc(wf.get());
    if (old_ch == EOF)                           return -1;
    if (fseeko(wf.get(), offset, SEEK_SET) != 0) return -1;
    if (fputc((int)new_val, wf.get()) == EOF)    return -1;
    if (fflush(wf.get()) != 0)                   return -1;

    *out_old_val = (unsigned char)old_ch;
    return 0;
}

bool is_file_held_by_other_process(const char *path)
{
    (void)path;
    return false;
}

// Folds mtime + nsec + size: macOS st_mtime is 1s resolution, so without nsec a same-second
// external writer with unchanged size would slip past HasExternalModification().
int64_t get_file_mtime_token(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    // Unsigned shifts: signed shift past the sign bit is UB.
    uint64_t mtime = (uint64_t)st.st_mtimespec.tv_sec;
    uint64_t nsec  = (uint64_t)st.st_mtimespec.tv_nsec;
    uint64_t size  = (uint64_t)st.st_size;
    return (int64_t)((mtime << 30) ^ (nsec << 4) ^ size);
}
