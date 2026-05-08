#include "fileops.h"

#include <sys/types.h>
#include <sys/stat.h>

#if defined(_WIN32)
#  include <share.h>
#  include <wchar.h>
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <string>

/* All caller-facing path arguments in this file are UTF-8 (the rest of
 * the codebase keeps paths in UTF-8 and round-trips them through
 * std::filesystem::u8path). The Windows ANSI APIs (_fsopen, CreateFileA,
 * _stat64) interpret bytes through the active code page (typically
 * CP1252), which mangles non-ASCII names like "résumé.bin", "日本語.dat",
 * or "Документ.exe". Convert to UTF-16 here and call the wide variants
 * so the editor opens what the user actually picked. */
static std::wstring Utf8ToWideLocal(const char *path) {
    if (!path || !*path) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path, -1, w.data(), n);
    return w;
}

static std::wstring ModeToWideLocal(const char *mode) {
    if (!mode) return std::wstring();
    std::wstring w;
    for (const char *p = mode; *p; ++p) {
        w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
    }
    return w;
}
#endif

/* Plain fopen on MSVC uses _SH_DENYWR, silently blocking external saves.
 * Share freely; detect external writes via the mtime watcher. */
FILE *open_file_shared(const char *path, const char *mode)
{
#if defined(_WIN32)
    std::wstring wpath = Utf8ToWideLocal(path);
    std::wstring wmode = ModeToWideLocal(mode);
    if (wpath.empty()) return nullptr;
    return _wfsopen(wpath.c_str(), wmode.c_str(), _SH_DENYNO);
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

/* Atomic read-then-write under a single handle. The FILE-based path used
 * by the original EditByte was:
 *
 *     1. fseek(read_handle, offset)
 *     2. fgetc → captures `old_val`
 *     3. open separate write_handle
 *     4. write `new_val`
 *
 * Between steps 2 and 4 a concurrent external writer could mutate the
 * byte, leaving the undo stack with a stale `old_val`. Closing that
 * window required either (a) re-reading inside the write handle or
 * (b) holding a lock across read+write. We do both on Windows: a
 * one-byte exclusive LockFileEx makes the read+write atomic against
 * other processes (including non-hxediter writers), and the
 * read-then-seek-then-write sequence runs under one HANDLE so no
 * close/reopen window remains. */
int replace_byte_at_path(const char *path, int64_t offset,
                         unsigned char new_val, unsigned char *out_old_val)
{
    if (out_old_val == NULL) return -1;
#if defined(_WIN32)
    std::wstring wpath = Utf8ToWideLocal(path);
    if (wpath.empty()) return -1;

    HANDLE h = CreateFileW(
        wpath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;

    /* LOCKFILE_EXCLUSIVE_LOCK on the single byte at `offset` — kernel-
     * enforced against other processes opening the same byte range.
     * Using LOCKFILE_FAIL_IMMEDIATELY: if some other writer holds
     * this byte, we fail fast instead of stalling the GUI thread.
     * The user can retry; "edit failed because something else is
     * holding it" is exactly the right message. */
    OVERLAPPED ov{};
    ov.Offset     = (DWORD)((uint64_t)offset & 0xFFFFFFFFu);
    ov.OffsetHigh = (DWORD)(((uint64_t)offset >> 32) & 0xFFFFFFFFu);
    if (!LockFileEx(h,
                    LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                    0, 1, 0, &ov)) {
        CloseHandle(h);
        return -1;
    }

    auto unlock_and_close = [&](int rc) {
        OVERLAPPED uov{};
        uov.Offset     = ov.Offset;
        uov.OffsetHigh = ov.OffsetHigh;
        UnlockFileEx(h, 0, 1, 0, &uov);
        CloseHandle(h);
        return rc;
    };

    LARGE_INTEGER pos;
    pos.QuadPart = offset;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) return unlock_and_close(-1);

    unsigned char old_byte = 0;
    DWORD got = 0;
    if (!ReadFile(h, &old_byte, 1, &got, NULL) || got != 1) {
        return unlock_and_close(-1);
    }

    /* ReadFile advanced the file pointer; rewind for the write. */
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) return unlock_and_close(-1);

    DWORD wrote = 0;
    if (!WriteFile(h, &new_val, 1, &wrote, NULL) || wrote != 1) {
        return unlock_and_close(-1);
    }

    *out_old_val = old_byte;
    return unlock_and_close(0);
#else
    FILE *wf = open_file_shared(path, "rb+");
    if (wf == NULL) return -1;

    if (fseek64(wf, offset, SEEK_SET) != 0) { fclose(wf); return -1; }
    int old_ch = fgetc(wf);
    if (old_ch == EOF)                       { fclose(wf); return -1; }
    if (fseek64(wf, offset, SEEK_SET) != 0)  { fclose(wf); return -1; }
    if (fputc((int)new_val, wf) == EOF)      { fclose(wf); return -1; }
    if (fflush(wf) != 0)                     { fclose(wf); return -1; }

    *out_old_val = (unsigned char)old_ch;
    fclose(wf);
    return 0;
#endif
}

/* Exclusive probe open; ERROR_SHARING_VIOLATION means another process
 * holds it. Other failures fall through to the caller's normal fopen. */
bool is_file_held_by_other_process(const char *path)
{
#if defined(_WIN32)
    std::wstring wpath = Utf8ToWideLocal(path);
    if (wpath.empty()) return false;
    HANDLE h = CreateFileW(
        wpath.c_str(),
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
 * granularity still change the token.
 *
 * Windows: GetFileAttributesExW returns FILETIME, which is 100-ns ticks
 * since 1601 — fine enough to distinguish two single-byte edits in the
 * same wall-clock second on NTFS. The previous implementation used
 * _wstat64 (1-second granularity), which lost a same-second edit pair
 * if the size also stayed unchanged: an external tool's overwrite of
 * one byte at the same second as ours, with the same size, looked
 * identical to a no-op and HasExternalModification() returned false.
 *
 * POSIX: kept on st_mtime (1s granularity) — same edge case exists in
 * theory but the current Linux/macOS dev build is not the primary
 * target and the workaround would mean per-file statx() handling. */
int64_t get_file_mtime_token(const char *path)
{
#if defined(_WIN32)
    std::wstring wpath = Utf8ToWideLocal(path);
    if (wpath.empty()) return -1;
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &attr)) {
        return -1;
    }
    uint64_t mtime = ((uint64_t)attr.ftLastWriteTime.dwHighDateTime << 32) |
                      (uint64_t)attr.ftLastWriteTime.dwLowDateTime;
    uint64_t size  = ((uint64_t)attr.nFileSizeHigh << 32) |
                      (uint64_t)attr.nFileSizeLow;
    return (int64_t)(mtime ^ size);
#else
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    int64_t mtime = (int64_t)st.st_mtime;
    int64_t size  = (int64_t)st.st_size;
    return (mtime << 20) ^ size;
#endif
}
