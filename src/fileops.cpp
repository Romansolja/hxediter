#include "fileops.h"

#include <sys/types.h>
#include <sys/stat.h>

#if defined(_WIN32)
#  include <share.h>
#endif

/* Plain fopen on MSVC uses _SH_DENYWR, silently blocking external saves.
 * Share freely; detect external writes via the mtime watcher. */
FILE *open_file_shared(const char *path, const char *mode)
{
#if defined(_WIN32)
    return _fsopen(path, mode, _SH_DENYNO);
#else
    return fopen(path, mode);
#endif
}

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

        /* Overlap by pattern_len-1 so a pattern straddling chunks hits. */
        pos += (int64_t)got - (pattern_len - 1);
    }

    return -1;
}

/* fflush surfaces deferred write errors here, not later as a confusing
 * seek failure. */
int write_byte_at(FILE *fp, int64_t offset, unsigned char val)
{
    if (fseek64(fp, offset, SEEK_SET) != 0) return -1;
    if (fputc((int)val, fp) == EOF)         return -1;
    if (fflush(fp) != 0)                    return -1;
    return 0;
}

/* Long-lived core handle stays read-only so external tools can still
 * open for write; each edit uses a transient write handle. */
int write_byte_at_path(const char *path, int64_t offset, unsigned char val)
{
    FILE *wf = open_file_shared(path, "rb+");
    if (wf == NULL) return -1;
    int rc = write_byte_at(wf, offset, val);
    fclose(wf);
    return rc;
}

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

/* Exclusive probe open; ERROR_SHARING_VIOLATION means another process
 * holds it. Other failures fall through to the caller's normal fopen. */
bool is_file_held_by_other_process(const char *path)
{
#if defined(_WIN32)
    HANDLE h = CreateFileA(
        path,
        GENERIC_READ,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        return false;
    }
    return GetLastError() == ERROR_SHARING_VIOLATION;
#else
    (void)path;
    return false;
#endif
}

/* mtime folded with size so truncations/appends within the same mtime
 * granularity still change the token. */
int64_t get_file_mtime_token(const char *path)
{
#if defined(_WIN32)
    struct _stat64 st;
    if (_stat64(path, &st) != 0) return -1;
#else
    struct stat st;
    if (stat(path, &st) != 0) return -1;
#endif
    int64_t mtime = (int64_t)st.st_mtime;
    int64_t size  = (int64_t)st.st_size;
    return (mtime << 20) ^ size;
}
