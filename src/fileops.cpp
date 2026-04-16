/* fileops.c — File I/O operations */

#include "fileops.h"

#include <sys/types.h>
#include <sys/stat.h>

#if defined(_WIN32)
#  include <share.h>   /* _SH_DENYNO */
#endif

/* Plain fopen on MSVC calls _fsopen with _SH_SECURE (= _SH_DENYWR), which
 * means "no other process may write to this file while I have it open."
 * That silently blocks Notepad/VS Code/etc. from saving over a file that
 * hxediter is currently viewing, and the user never sees a hint why.
 * We want the opposite: share freely, and catch any external write via
 * the mtime watcher in HexEditorCore. */
FILE *open_file_shared(const char *path, const char *mode)
{
#if defined(_WIN32)
    return _fsopen(path, mode, _SH_DENYNO);
#else
    return fopen(path, mode);
#endif
}

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
            unsigned char *match = (unsigned char *)memchr(search_ptr, pattern[0],
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

/* Two-handle write path: the long-lived HexEditorCore handle stays
 * read-only so external tools (Notepad, VSCode, git, antivirus) can open
 * the file for write without hitting a sharing violation. Each byte edit
 * briefly opens its own write handle, applies the patch, and closes. */
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

/* Probes the file with an exclusive (no-share) open. On Windows, if another
 * process already holds a handle to it, CreateFileA fails with
 * ERROR_SHARING_VIOLATION — that's our signal. Other failures (missing file,
 * permission denied) are left for the caller's normal fopen path to report. */
bool is_file_held_by_other_process(const char *path)
{
#if defined(_WIN32)
    HANDLE h = CreateFileA(
        path,
        GENERIC_READ,
        0,                      /* dwShareMode = 0: deny all sharing */
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

/* Folds mtime (seconds) and file size into a single 64-bit token we can
 * compare against a baseline stored at open time. Including size means we
 * also detect third-party truncations/appends that happen to land in the
 * same mtime granularity. Returns -1 if the file can't be stat'd. */
int64_t get_file_mtime_token(const char *path)
{
#if defined(_WIN32)
    struct _stat64 st;
    if (_stat64(path, &st) != 0) return -1;
#else
    struct stat st;
    if (stat(path, &st) != 0) return -1;
#endif
    /* Use a simple mix that stays within int64_t without risk of overflow.
     * mtime fits comfortably in 32 bits for any realistic date; size is
     * XOR'd in at a high bit so equal mtimes with different sizes differ. */
    int64_t mtime = (int64_t)st.st_mtime;
    int64_t size  = (int64_t)st.st_size;
    return (mtime << 20) ^ size;
}
