#ifndef FILEOPS_H
#define FILEOPS_H

#include "platform.h"

FILE   *open_file_shared(const char *path, const char *mode);

int64_t get_file_size(FILE *fp);
int64_t search_bytes(FILE *fp, int64_t file_size,
                     int64_t start, const unsigned char *pattern, int pattern_len);
int write_byte_at(FILE *fp, int64_t offset, unsigned char val);

// Open, patch, close — hxediter never holds write access when idle. 0 on success, -1 otherwise.
int write_byte_at_path(const char *path, int64_t offset, unsigned char val);

// Atomic byte read+write under one rb+ FILE*. Writes the previous byte to *out_old_val.
// No OS-level lock — concurrent external writers are deliberately allowed.
int replace_byte_at_path(const char *path, int64_t offset,
                         unsigned char new_val, unsigned char *out_old_val);

// Always false on POSIX — no OS-enforced sharing mode at open time.
bool is_file_held_by_other_process(const char *path);

// mtime+size equality token. -1 on error. Not portable across machines.
int64_t get_file_mtime_token(const char *path);

// --- Block/character device inspection (read-only) -------------------------
// A disk node is opened read-only so users can inspect raw bytes. The specifics
// (non-blocking open, ioctl sizing, read alignment) stay in the IO layer.

// Open a device O_RDONLY without blocking on the open itself — an exotic char
// device (tty/serial) would otherwise block waiting for carrier. O_NONBLOCK is
// cleared right after so steady-state reads behave normally. Buffered FILE* on
// success (caller owns), NULL otherwise.
FILE *open_device_ro(const char *path);

// Total addressable size of a block/character device in bytes (block_size ×
// block_count via DKIOC ioctls). -1 if the device reports no fixed extent
// (e.g. /dev/random) or the query fails — such a device is not inspectable.
int64_t get_device_size(FILE *fp);

// Logical block size used to align device reads, or 0 if unavailable.
int64_t get_device_block_size(FILE *fp);

// Block-aligned read window covering [offset, offset+count) on a device of
// `device_size` bytes (itself a block multiple). Raw character devices reject
// unaligned seeks/lengths, so this rounds out to whole blocks and reports where
// the requested bytes start inside the window. Pure — unit-tested in isolation.
struct DeviceReadWindow {
    int64_t start;   // block-aligned seek position
    int64_t length;  // block-aligned read length (clamped to device_size)
    int64_t slice;   // offset of the requested bytes within the read buffer
};
DeviceReadWindow align_device_read(int64_t offset, int64_t count,
                                   int64_t device_size, int64_t block_size);

#endif
