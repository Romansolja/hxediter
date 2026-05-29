#include "hex_editor_core.h"
#include "fileops.h"
#include "path_utils.h"
#include "undo.h"

#include <filesystem>
#include <stdexcept>
#include <system_error>

// Opens the read handle, switches it to _IONBF, and probes file size.
// Centralizes the five-step ritual the constructor and ReloadFromDisk used
// to duplicate. Throws std::runtime_error when `throw_on_failure` is true
// (constructor path); returns std::nullopt when false (reload path).
//
// _IONBF: edits go through a separate write FILE*, and macOS keeps stdio's
// read buffer valid across fseek into already-buffered ranges — ReadAt
// would serve stale bytes after an edit. Refuse to open rather than fall
// back to default buffering silently.
std::optional<HexEditorCore::OpenedFile>
HexEditorCore::OpenAndProbe(bool throw_on_failure)
{
    if (is_device_) {
        // Guarded (non-blocking) open; unbuffered so stdio can't issue an
        // unaligned readahead a raw device would reject — ReadAt aligns itself.
        FileHandle dfp(open_device_ro(state_.filename));
        if (!dfp) {
            if (throw_on_failure)
                throw std::runtime_error("Cannot open device '" + filename_storage_ + "'");
            return std::nullopt;
        }
        setvbuf(dfp.get(), nullptr, _IONBF, 0);
        int64_t dsize = get_device_size(dfp.get());
        int64_t dbs   = get_device_block_size(dfp.get());
        if (dsize <= 0 || dbs <= 0) {
            if (throw_on_failure)
                throw std::runtime_error(
                    "'" + filename_storage_ + "' is a device with no readable "
                    "fixed size — nothing to inspect.");
            return std::nullopt;
        }
        return OpenedFile{std::move(dfp), dsize, dbs};
    }

    FileHandle fp(open_file_shared(state_.filename, "rb"));
    if (!fp) {
        if (throw_on_failure)
            throw std::runtime_error("Cannot open '" + filename_storage_ + "'");
        return std::nullopt;
    }

    if (setvbuf(fp.get(), nullptr, _IONBF, 0) != 0) {
        if (throw_on_failure)
            throw std::runtime_error(
                "Cannot disable buffering on '" + filename_storage_ +
                "' (setvbuf failed); refusing to open with default "
                "buffering on macOS — would render stale bytes after edits.");
        return std::nullopt;
    }

    int64_t size = get_file_size(fp.get());
    if (size < 0) {
        if (throw_on_failure)
            throw std::runtime_error("Cannot determine file size");
        return std::nullopt;
    }

    return OpenedFile{std::move(fp), size, 0};
}

HexEditorCore::HexEditorCore(const std::string& filename)
    : state_{}, filename_storage_(filename)
{
    state_.filename = filename_storage_.c_str();

    // macOS fopen() on a directory succeeds silently as a 0-byte file — reject up front.
    {
        std::error_code ec;
        auto fsp    = PathFromUtf8(filename);
        auto status = std::filesystem::status(fsp, ec);
        if (!ec && std::filesystem::is_directory(status)) {
            // Match by extension (not HFS bundle bit) so cross-platform .app symlink drops work.
            const std::string ext = fsp.extension().string();
            const bool is_bundle = (ext == ".app"       ||
                                    ext == ".framework" ||
                                    ext == ".bundle"    ||
                                    ext == ".kext");
            std::string msg = "'" + filename + "' is a directory, not a file.";
            if (is_bundle) {
                msg += " On macOS, open the package (right-click → "
                       "Show Package Contents in Finder) and pick a "
                       "file inside.";
            }
            throw std::runtime_error(msg);
        }

        // Classify before any fopen. A block/character device (a disk node) is
        // opened read-only for inspection via the guarded device path in
        // OpenAndProbe. FIFOs and sockets are streams with no seekable extent —
        // and fopen("rb") on a FIFO would block the UI thread forever waiting
        // for a writer — so reject them up front. status() follows symlinks, so
        // a symlink resolves to its target; a broken symlink (not exists) falls
        // through to the open error below. (drag-drop / argv / Apple Events
        // still pre-filter to regular files; only the open dialog reaches here.)
        if (!ec && std::filesystem::exists(status)) {
            if (std::filesystem::is_block_file(status) ||
                std::filesystem::is_character_file(status)) {
                is_device_ = true;
            } else if (!std::filesystem::is_regular_file(status)) {
                throw std::runtime_error(
                    "'" + filename + "' is not a regular file or device "
                    "(it is a pipe or socket). hxediter cannot open it.");
            }
        }
    }

    if (is_file_held_by_other_process(state_.filename)) {
        throw std::runtime_error(
            "'" + filename + "' is open in another program. "
            "Close it there first, then try again.");
    }

    // Devices open read-only for inspection: never probe rb+ (it could block on
    // an exotic char device, and writing a raw disk needs alignment we don't do).
    if (is_device_) {
        state_.is_readonly = 1;
        forced_readonly_   = true;   // one-way latch — survives ReloadFromDisk
    } else {
        // Probe writability, then keep only a read handle open — edits use transient write handles.
        FileHandle probe(open_file_shared(state_.filename, "rb+"));
        state_.is_readonly = probe ? 0 : 1;
    }

    // fp_ is UI-thread-only: get_file_size, Search, and ReadAt all seek on it.
    auto opened        = OpenAndProbe(/*throw_on_failure=*/true);
    fp_                = std::move(opened->fp);
    state_.file_size   = opened->size;
    device_block_size_ = opened->block_size;

    // -1 means stat failed — degrade to no-detection rather than fail open.
    baseline_token_ = get_file_mtime_token(state_.filename);
}

HexEditorCore::~HexEditorCore() = default;

std::vector<unsigned char> HexEditorCore::ReadAt(int64_t offset, size_t count) const
{
    if (offset < 0 || offset >= state_.file_size || count == 0)
        return {};

    int64_t avail = state_.file_size - offset;
    if ((int64_t)count > avail) count = (size_t)avail;

    if (device_block_size_ > 0) {
        // Raw devices reject unaligned seeks/lengths: read the block-aligned
        // window covering the request, then slice the requested bytes out.
        DeviceReadWindow w = align_device_read(offset, (int64_t)count,
                                               state_.file_size,
                                               device_block_size_);
        if (w.length <= 0)
            return {};
        std::vector<unsigned char> window((size_t)w.length);
        if (fseeko(fp_.get(), w.start, SEEK_SET) != 0)
            return {};
        size_t got = fread(window.data(), 1, (size_t)w.length, fp_.get());
        if ((int64_t)got <= w.slice)
            return {};
        int64_t have = (int64_t)got - w.slice;
        if (have > (int64_t)count) have = (int64_t)count;
        return std::vector<unsigned char>(
            window.begin() + w.slice,
            window.begin() + w.slice + have);
    }

    std::vector<unsigned char> buf(count);
    if (fseeko(fp_.get(), offset, SEEK_SET) != 0)
        return {};

    size_t got = fread(buf.data(), 1, count, fp_.get());
    buf.resize(got);
    return buf;
}

std::optional<EditResult> HexEditorCore::EditByte(int64_t offset, unsigned char new_val)
{
    if (state_.is_readonly)
        return std::nullopt;

    if (offset < 0 || offset >= state_.file_size)
        return std::nullopt;

    unsigned char old_val = 0;
    if (replace_byte_at_path(state_.filename, offset, new_val, &old_val) != 0)
        return std::nullopt;

    undo_push(&state_, offset, old_val, new_val);

    // Rebaseline so our own write doesn't trip HasExternalModification.
    baseline_token_ = get_file_mtime_token(state_.filename);

    return EditResult{offset, old_val, new_val};
}

std::optional<UndoResult> HexEditorCore::Undo()
{
    UndoEntry entry;

    if (!undo_pop(&state_, &entry))
        return std::nullopt;

    if (write_byte_at_path(state_.filename, entry.offset, entry.old_val) != 0) {
        undo_unpop(&state_);
        return std::nullopt;
    }

    baseline_token_ = get_file_mtime_token(state_.filename);

    return UndoResult{entry.offset, entry.old_val, entry.new_val, state_.undo_count};
}

std::optional<SearchResult> HexEditorCore::Search(const std::vector<unsigned char>& pattern,
                                                   int64_t start_offset)
{
    if (pattern.empty())
        return std::nullopt;

    int64_t result = search_bytes(fp_.get(), state_.file_size,
                                  start_offset, pattern.data(),
                                  static_cast<int>(pattern.size()));

    if (result == -1 && start_offset > 0) {
        result = search_bytes(fp_.get(), state_.file_size, 0,
                              pattern.data(), static_cast<int>(pattern.size()));
    }

    if (result == -1)
        return std::nullopt;

    return SearchResult{result};
}

int64_t HexEditorCore::GetFileSize() const
{
    return state_.file_size;
}

std::string HexEditorCore::GetFilename() const
{
    return filename_storage_;
}

bool HexEditorCore::IsReadOnly() const
{
    return state_.is_readonly != 0;
}

void HexEditorCore::ForceReadOnly()
{
    state_.is_readonly = 1;
    forced_readonly_   = true;
}

int HexEditorCore::GetUndoCount() const
{
    return state_.undo_count;
}

bool HexEditorCore::HasExternalModification() const
{
    if (baseline_token_ == -1) return false;
    int64_t now = get_file_mtime_token(state_.filename);
    if (now == -1) return false;
    return now != baseline_token_;
}

void HexEditorCore::Rebaseline()
{
    baseline_token_ = get_file_mtime_token(state_.filename);
}

// Re-probe size from the open read handle without touching bytes or undo.
// "Keep my edits" accepts an external resize without reopening, so the cached
// file_size (and the caret range derived from it) would otherwise keep lying.
void HexEditorCore::RefreshFileSize()
{
    if (!fp_) return;
    int64_t size = get_file_size(fp_.get());
    if (size >= 0) state_.file_size = size;
}

bool HexEditorCore::ReloadFromDisk()
{
    if (!fp_) return false;

    // An atomic replace (write-temp + rename) unlinks the original inode and
    // points filename_ at a new one. Our existing FILE* keeps the old inode
    // alive — reads would still serve stale bytes, while edits/undo (which
    // write by path) would land on the new inode. Close and reopen so reads
    // and writes share an inode again.
    fp_.reset();

    auto opened = OpenAndProbe(/*throw_on_failure=*/false);
    if (!opened) return false;
    fp_                = std::move(opened->fp);
    state_.file_size   = opened->size;
    device_block_size_ = opened->block_size;

    // Drop undo — old offsets may no longer refer to the same bytes.
    state_.undo_count = 0;
    state_.undo_head  = 0;

    // Re-probe writability: the new inode can have different perms than the
    // original. ForceReadOnly is a one-way latch and stays on regardless.
    if (!forced_readonly_) {
        FileHandle probe(open_file_shared(state_.filename, "rb+"));
        state_.is_readonly = probe ? 0 : 1;
    }

    baseline_token_ = get_file_mtime_token(state_.filename);
    return true;
}
