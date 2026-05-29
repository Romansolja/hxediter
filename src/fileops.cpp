#include "fileops.h"
#include "file_handle.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/disk.h>

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
    // Refuse to write past EOF. hxediter is overwrite-only — the file size never
    // changes — but fseeko-past-EOF + fputc would EXTEND the file with a sparse
    // hole. This guards the undo replay: if an external process shrinks the file
    // below an edited offset, restoring the recorded byte must fail cleanly, not
    // silently regrow the file. (replace_byte_at_path's leading fgetc already
    // returns EOF past the end — this is the matching guard for the write path.)
    int64_t size = get_file_size(fp);
    if (size < 0 || offset < 0 || offset >= size) return -1;
    if (fseeko(fp, offset, SEEK_SET) != 0) return -1;
    if (fputc((int)val, fp) == EOF)         return -1;
    if (fflush(fp) != 0)                    return -1;
    return 0;
}

int write_byte_at_path(const char *path, int64_t offset, unsigned char val)
{
    FileHandle wf(open_file_shared(path, "rb+"));
    if (!wf) return -1;
    // Unbuffered like the read handle (see HexEditorCore::OpenAndProbe): a write
    // fault then surfaces at fputc/fflush instead of being deferred into the
    // FileHandle destructor's silent fclose, where the error would be lost.
    setvbuf(wf.get(), NULL, _IONBF, 0);
    return write_byte_at(wf.get(), offset, val);
}

int replace_byte_at_path(const char *path, int64_t offset,
                         unsigned char new_val, unsigned char *out_old_val)
{
    if (out_old_val == NULL) return -1;
    FileHandle wf(open_file_shared(path, "rb+"));
    if (!wf) return -1;
    // Unbuffered: a write fault surfaces at fputc/fflush rather than being
    // deferred into FileHandle's silent fclose (matches the read handle).
    setvbuf(wf.get(), NULL, _IONBF, 0);

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

FILE *open_device_ro(const char *path)
{
    // O_NONBLOCK guards the open() itself — a tty/serial char device would
    // otherwise block waiting for carrier. Disk devices ignore it; we clear it
    // straight after so the subsequent reads behave like a normal file.
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) return NULL;
    int fl = fcntl(fd, F_GETFL);
    if (fl >= 0) fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
    FILE *fp = fdopen(fd, "rb");
    if (!fp) { close(fd); return NULL; }
    return fp;
}

int64_t get_device_size(FILE *fp)
{
    // macOS lseek(SEEK_END) on a raw device returns 0 — the size lives behind
    // the DKIOC ioctls instead.
    uint32_t block_size  = 0;
    uint64_t block_count = 0;
    int fd = fileno(fp);
    if (ioctl(fd, DKIOCGETBLOCKSIZE,  &block_size)  != 0) return -1;
    if (ioctl(fd, DKIOCGETBLOCKCOUNT, &block_count) != 0) return -1;
    if (block_size == 0 || block_count == 0) return -1;
    // Guard the multiply against wrapping into a negative int64.
    if (block_count > (uint64_t)INT64_MAX / block_size) return -1;
    return (int64_t)(block_count * (uint64_t)block_size);
}

int64_t get_device_block_size(FILE *fp)
{
    uint32_t block_size = 0;
    if (ioctl(fileno(fp), DKIOCGETBLOCKSIZE, &block_size) != 0) return 0;
    return (int64_t)block_size;
}

DeviceReadWindow align_device_read(int64_t offset, int64_t count,
                                   int64_t device_size, int64_t block_size)
{
    DeviceReadWindow w{0, 0, 0};
    if (block_size <= 0 || device_size <= 0 ||
        offset < 0 || offset >= device_size || count <= 0)
        return w;
    if (count > device_size - offset) count = device_size - offset;

    int64_t start = offset - (offset % block_size);
    int64_t end   = offset + count;
    if (end % block_size != 0) end += block_size - (end % block_size);
    if (end > device_size) end = device_size;   // device_size is a block multiple

    w.start  = start;
    w.length = end - start;
    w.slice  = offset - start;
    return w;
}
