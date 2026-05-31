#include "test_runner.h"
#include "hex_editor_core.h"
#include "fileops.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

static std::string make_temp_file(const std::vector<unsigned char>& contents) {
    // Mix pid + nanoseconds so parallel ctest jobs don't collide on the same path.
    auto ns = std::chrono::steady_clock::now().time_since_epoch().count();
    auto name = "hxediter_test_" + std::to_string(getpid()) + "_" +
                std::to_string(ns) + "_" + std::to_string(std::rand()) + ".bin";
    auto path = fs::temp_directory_path() / name;
    FILE* fp = std::fopen(path.string().c_str(), "wb");
    if (!fp) return "";
    if (!contents.empty())
        std::fwrite(contents.data(), 1, contents.size(), fp);
    std::fclose(fp);
    return path.string();
}

static void test_read_at() {
    auto path = make_temp_file({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});
    HX_TEST_ASSERT(!path.empty());
    {
        HexEditorCore core(path);
        HX_TEST_ASSERT_EQ(core.GetFileSize(), (int64_t)6);

        auto r = core.ReadAt(0, 6);
        HX_TEST_ASSERT_EQ(r.size(), (size_t)6);
        if (r.size() == 6) {
            HX_TEST_ASSERT_EQ((int)r[0], 0x00);
            HX_TEST_ASSERT_EQ((int)r[5], 0x55);
        }

        // Middle slice.
        auto r2 = core.ReadAt(2, 3);
        HX_TEST_ASSERT_EQ(r2.size(), (size_t)3);
        if (r2.size() == 3) {
            HX_TEST_ASSERT_EQ((int)r2[0], 0x22);
            HX_TEST_ASSERT_EQ((int)r2[2], 0x44);
        }

        // Out-of-range / zero-count.
        HX_TEST_ASSERT_EQ(core.ReadAt(-1, 1).size(), (size_t)0);
        HX_TEST_ASSERT_EQ(core.ReadAt(100, 1).size(), (size_t)0);
        HX_TEST_ASSERT_EQ(core.ReadAt(0, 0).size(), (size_t)0);

        // Overshoots EOF — truncated to remaining bytes.
        auto r3 = core.ReadAt(4, 100);
        HX_TEST_ASSERT_EQ(r3.size(), (size_t)2);
    }
    fs::remove(path);
}

static void test_edit_and_undo() {
    auto path = make_temp_file({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});
    HX_TEST_ASSERT(!path.empty());
    {
        HexEditorCore core(path);

        auto e = core.EditByte(2, 0xAA);
        HX_TEST_ASSERT(e.has_value());
        if (e) {
            HX_TEST_ASSERT_EQ((int)e->old_val, 0x22);
            HX_TEST_ASSERT_EQ((int)e->new_val, 0xAA);
        }

        auto r = core.ReadAt(2, 1);
        HX_TEST_ASSERT_EQ(r.size(), (size_t)1);
        if (r.size() == 1) HX_TEST_ASSERT_EQ((int)r[0], 0xAA);

        // Undo restores the prior byte and reports the swap.
        auto u = core.Undo();
        HX_TEST_ASSERT(u.has_value());
        if (u) {
            HX_TEST_ASSERT_EQ((int)u->restored_val, 0x22);
            HX_TEST_ASSERT_EQ((int)u->undone_val,   0xAA);
            HX_TEST_ASSERT_EQ(u->offset,            (int64_t)2);
        }

        auto r2 = core.ReadAt(2, 1);
        HX_TEST_ASSERT_EQ(r2.size(), (size_t)1);
        if (r2.size() == 1) HX_TEST_ASSERT_EQ((int)r2[0], 0x22);

        // Empty stack returns nullopt.
        HX_TEST_ASSERT(!core.Undo().has_value());

        // Out-of-range / negative offsets are rejected without touching the file.
        HX_TEST_ASSERT(!core.EditByte(-1, 0xFF).has_value());
        HX_TEST_ASSERT(!core.EditByte(1000, 0xFF).has_value());
    }
    fs::remove(path);
}

static void test_search() {
    auto path = make_temp_file({0x00, 0x11, 0x22, 0x33, 0x44, 0x22, 0x33});
    HX_TEST_ASSERT(!path.empty());
    {
        HexEditorCore core(path);
        std::vector<unsigned char> pat = {0x22, 0x33};

        // Hit at offset 2.
        auto s = core.Search(pat, 0);
        HX_TEST_ASSERT(s.has_value());
        if (s) HX_TEST_ASSERT_EQ(s->offset, (int64_t)2);

        // Hit at offset 5 (the second occurrence) when starting past the first.
        auto s2 = core.Search(pat, 3);
        HX_TEST_ASSERT(s2.has_value());
        if (s2) HX_TEST_ASSERT_EQ(s2->offset, (int64_t)5);

        // Past the last hit — Search wraps to the start of the file.
        auto s3 = core.Search(pat, 6);
        HX_TEST_ASSERT(s3.has_value());
        if (s3) HX_TEST_ASSERT_EQ(s3->offset, (int64_t)2);

        // Pattern not present.
        std::vector<unsigned char> miss = {0xDE, 0xAD, 0xBE, 0xEF};
        HX_TEST_ASSERT(!core.Search(miss, 0).has_value());

        // Empty pattern is rejected (caller's invariant).
        std::vector<unsigned char> empty_pat;
        HX_TEST_ASSERT(!core.Search(empty_pat, 0).has_value());
    }
    fs::remove(path);
}

static void test_external_modification() {
    auto path = make_temp_file({0x00, 0x11, 0x22, 0x33});
    HX_TEST_ASSERT(!path.empty());
    {
        HexEditorCore core(path);
        HX_TEST_ASSERT(!core.HasExternalModification());

        // Append a byte through a separate FILE* — mtime token mixes mtime,
        // nsec, and size, so growing the file guarantees the token changes
        // regardless of mtime resolution on the host filesystem.
        {
            FILE* fp = std::fopen(path.c_str(), "rb+");
            HX_TEST_ASSERT(fp != nullptr);
            if (fp) {
                std::fseek(fp, 0, SEEK_END);
                std::fputc(0xEE, fp);
                std::fclose(fp);
            }
        }
        HX_TEST_ASSERT(core.HasExternalModification());

        // Rebaseline clears the flag without changing reported state.
        core.Rebaseline();
        HX_TEST_ASSERT(!core.HasExternalModification());

        // ReloadFromDisk picks up the new size and clears undo.
        // First, queue an edit so we can prove the undo stack drops.
        auto e = core.EditByte(0, 0xCC);
        HX_TEST_ASSERT(e.has_value());
        HX_TEST_ASSERT(core.GetUndoCount() > 0);

        // Grow the file again to force a size change.
        {
            FILE* fp = std::fopen(path.c_str(), "rb+");
            if (fp) {
                std::fseek(fp, 0, SEEK_END);
                std::fputc(0xDD, fp);
                std::fclose(fp);
            }
        }
        HX_TEST_ASSERT(core.ReloadFromDisk());
        HX_TEST_ASSERT(!core.HasExternalModification());
        HX_TEST_ASSERT_EQ(core.GetUndoCount(), 0);
        HX_TEST_ASSERT_EQ(core.GetFileSize(), (int64_t)6);
    }
    fs::remove(path);
}

static void test_atomic_replace_reload() {
    // The bug this guards against: an external "save as new file then rename"
    // pattern (used by most editors) unlinks the inode our FILE* points at,
    // while edits/undo (which open by path) hit the new inode. Without
    // reopening on ReloadFromDisk, reads kept showing pre-replace bytes.
    auto path = make_temp_file({0x00, 0x11, 0x22});
    HX_TEST_ASSERT(!path.empty());
    {
        HexEditorCore core(path);
        HX_TEST_ASSERT_EQ(core.GetFileSize(), (int64_t)3);

        // Atomic replace via temp + rename.
        auto tmp = path + ".tmp";
        {
            FILE* fp = std::fopen(tmp.c_str(), "wb");
            HX_TEST_ASSERT(fp != nullptr);
            if (fp) {
                unsigned char content[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
                std::fwrite(content, 1, sizeof(content), fp);
                std::fclose(fp);
            }
        }
        HX_TEST_ASSERT_EQ(std::rename(tmp.c_str(), path.c_str()), 0);

        // ReloadFromDisk must reopen so reads see the new inode.
        HX_TEST_ASSERT(core.ReloadFromDisk());
        HX_TEST_ASSERT_EQ(core.GetFileSize(), (int64_t)5);

        auto r = core.ReadAt(0, 5);
        HX_TEST_ASSERT_EQ(r.size(), (size_t)5);
        if (r.size() == 5) {
            HX_TEST_ASSERT_EQ((int)r[0], 0xAA);
            HX_TEST_ASSERT_EQ((int)r[4], 0xEE);
        }

        // Writes hit the new inode, and the subsequent read returns the
        // freshly-written byte (no stale-buffer regression).
        auto e = core.EditByte(0, 0x11);
        HX_TEST_ASSERT(e.has_value());
        if (e) HX_TEST_ASSERT_EQ((int)e->old_val, 0xAA);
        auto r2 = core.ReadAt(0, 1);
        HX_TEST_ASSERT_EQ(r2.size(), (size_t)1);
        if (r2.size() == 1) HX_TEST_ASSERT_EQ((int)r2[0], 0x11);
    }
    fs::remove(path);
}

static void test_force_readonly_survives_reload() {
    // ForceReadOnly is documented as one-way. The reload's writability re-probe
    // must respect that even if the new inode is writable on disk.
    auto path = make_temp_file({0x00});
    HX_TEST_ASSERT(!path.empty());
    {
        HexEditorCore core(path);
        HX_TEST_ASSERT(!core.IsReadOnly());
        core.ForceReadOnly();
        HX_TEST_ASSERT(core.IsReadOnly());
        HX_TEST_ASSERT(core.ReloadFromDisk());
        HX_TEST_ASSERT(core.IsReadOnly());
    }
    fs::remove(path);
}

static void test_directory_open_rejected() {
    // Opening a directory used to silently succeed as a 0-byte file on macOS;
    // construction must throw instead.
    auto dir = fs::temp_directory_path() /
               ("hxediter_test_dir_" + std::to_string(std::rand()));
    fs::create_directory(dir);
    bool threw = false;
    try {
        HexEditorCore core(dir.string());
    } catch (const std::runtime_error&) {
        threw = true;
    } catch (...) {
        // Any other exception type is wrong.
    }
    HX_TEST_ASSERT(threw);
    fs::remove(dir);
}

static void test_missing_file_rejected() {
    bool threw = false;
    try {
        HexEditorCore core("/this/path/should/not/exist/hxediter_xyz.bin");
    } catch (const std::runtime_error&) {
        threw = true;
    } catch (...) {
    }
    HX_TEST_ASSERT(threw);
}

static void test_undo_does_not_extend_truncated_file() {
    // Regression: edit a byte, let an external process shrink the file below
    // that offset, then undo. The undo write path must REFUSE the past-EOF
    // offset rather than fseeko-past-EOF + fputc, which would silently regrow
    // the file with a zero-filled sparse hole (data corruption).
    auto path = make_temp_file(std::vector<unsigned char>(64, 0x55));
    HX_TEST_ASSERT(!path.empty());
    {
        HexEditorCore core(path);
        HX_TEST_ASSERT_EQ(core.GetFileSize(), (int64_t)64);

        // Edit near the end, creating an undo entry at offset 48.
        auto e = core.EditByte(48, 0xAA);
        HX_TEST_ASSERT(e.has_value());
        HX_TEST_ASSERT_EQ(core.GetUndoCount(), 1);

        // External shrink to 16 bytes — below the edited offset.
        HX_TEST_ASSERT_EQ(truncate(path.c_str(), 16), 0);
        HX_TEST_ASSERT_EQ((int64_t)fs::file_size(path), (int64_t)16);

        // Undo must fail without touching the file size.
        HX_TEST_ASSERT(!core.Undo().has_value());
        HX_TEST_ASSERT_EQ((int64_t)fs::file_size(path), (int64_t)16);
        // undo_unpop restored the entry, so it can be retried after a reload.
        HX_TEST_ASSERT_EQ(core.GetUndoCount(), 1);
    }
    fs::remove(path);
}

static void test_fifo_rejected() {
    // A FIFO must be rejected at construction. Classification runs through
    // std::filesystem::status (stat-based) BEFORE any fopen, so the editor never
    // reaches the fopen("rb") that would block forever waiting for a writer.
    auto fifo = fs::temp_directory_path() /
                ("hxediter_test_fifo_" + std::to_string(getpid()) + "_" +
                 std::to_string(std::rand()));
    if (mkfifo(fifo.string().c_str(), 0600) != 0) return;  // skip if unsupported
    bool threw = false;
    try {
        HexEditorCore core(fifo.string());
    } catch (const std::runtime_error&) {
        threw = true;
    } catch (...) {
    }
    fs::remove(fifo);
    HX_TEST_ASSERT(threw);
}

static void test_device_without_size_rejected() {
    // /dev/null is a character device, so it is classified as a device — but the
    // DKIOC block-count query fails on it, so the guarded device path rejects it:
    // there is no seekable extent to inspect.
    const char* dev = "/dev/null";
    if (!fs::exists(dev)) return;  // skip where unavailable
    bool threw = false;
    try {
        HexEditorCore core(dev);
    } catch (const std::runtime_error&) {
        threw = true;
    } catch (...) {
    }
    HX_TEST_ASSERT(threw);
}

static void test_align_device_read() {
    // Pure block-alignment math for raw-device reads (block size 512 within a
    // 4096-byte device). This is the part of device inspection that real
    // hardware/root access would otherwise be needed to exercise.
    auto w = align_device_read(0, 512, 4096, 512);      // aligned, one whole block
    HX_TEST_ASSERT_EQ(w.start,  (int64_t)0);
    HX_TEST_ASSERT_EQ(w.length, (int64_t)512);
    HX_TEST_ASSERT_EQ(w.slice,  (int64_t)0);

    w = align_device_read(100, 16, 4096, 512);          // unaligned within block 0
    HX_TEST_ASSERT_EQ(w.start,  (int64_t)0);
    HX_TEST_ASSERT_EQ(w.length, (int64_t)512);
    HX_TEST_ASSERT_EQ(w.slice,  (int64_t)100);

    w = align_device_read(500, 24, 4096, 512);          // straddles a block boundary
    HX_TEST_ASSERT_EQ(w.start,  (int64_t)0);
    HX_TEST_ASSERT_EQ(w.length, (int64_t)1024);
    HX_TEST_ASSERT_EQ(w.slice,  (int64_t)500);

    w = align_device_read(4090, 16, 4096, 512);         // clamps at the device end
    HX_TEST_ASSERT_EQ(w.start,  (int64_t)3584);
    HX_TEST_ASSERT_EQ(w.length, (int64_t)512);
    HX_TEST_ASSERT_EQ(w.slice,  (int64_t)506);

    // Degenerate inputs yield an empty window.
    HX_TEST_ASSERT_EQ(align_device_read(-1, 16, 4096, 512).length,   (int64_t)0);
    HX_TEST_ASSERT_EQ(align_device_read(0, 16, 4096, 0).length,      (int64_t)0);
    HX_TEST_ASSERT_EQ(align_device_read(4096, 16, 4096, 512).length, (int64_t)0);
}

static void test_find_in_buffer() {
    // Shared scan helper behind both search_bytes (regular files) and the
    // device search loop. Exercises start/middle/end hits, absence, a
    // false-first-byte case, and degenerate lengths.
    const unsigned char buf[] = {0x00, 0x11, 0x22, 0x33, 0x22, 0x33, 0x44};
    const size_t n = sizeof(buf);

    unsigned char p_mid[] = {0x22, 0x33};
    HX_TEST_ASSERT_EQ(find_in_buffer(buf, n, p_mid, 2), (int64_t)2);   // first occurrence
    unsigned char p_end[] = {0x44};
    HX_TEST_ASSERT_EQ(find_in_buffer(buf, n, p_end, 1), (int64_t)6);   // last byte
    unsigned char p_begin[] = {0x00};
    HX_TEST_ASSERT_EQ(find_in_buffer(buf, n, p_begin, 1), (int64_t)0); // first byte
    unsigned char p_miss[] = {0xDE, 0xAD};
    HX_TEST_ASSERT_EQ(find_in_buffer(buf, n, p_miss, 2), (int64_t)-1); // absent

    // First byte matches repeatedly but the full pattern only matches later.
    const unsigned char buf2[] = {0x22, 0x00, 0x22, 0x33};
    HX_TEST_ASSERT_EQ(find_in_buffer(buf2, sizeof(buf2), p_mid, 2), (int64_t)2);

    // Degenerate: pattern longer than the buffer, and zero length.
    unsigned char p_long[] = {0x00, 0x11, 0x22};
    HX_TEST_ASSERT_EQ(find_in_buffer(buf, 2, p_long, 3), (int64_t)-1);
    HX_TEST_ASSERT_EQ(find_in_buffer(buf, n, p_mid, 0), (int64_t)-1);
}

int main() {
    std::srand((unsigned)std::time(nullptr));
    test_read_at();
    test_edit_and_undo();
    test_search();
    test_external_modification();
    test_atomic_replace_reload();
    test_force_readonly_survives_reload();
    test_directory_open_rejected();
    test_missing_file_rejected();
    test_undo_does_not_extend_truncated_file();
    test_fifo_rejected();
    test_device_without_size_rejected();
    test_align_device_read();
    test_find_in_buffer();
    return tests::run_all();
}
