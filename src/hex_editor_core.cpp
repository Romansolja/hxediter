#include "hex_editor_core.h"
#include "fileops.h"
#include "path_utils.h"
#include "undo.h"

#include <filesystem>
#include <stdexcept>
#include <system_error>

HexEditorCore::HexEditorCore(const std::string& filename)
    : state_{}, filename_storage_(filename)
{
    state_.filename = filename_storage_.c_str();

    /* Reject directories up front. On macOS, fopen() on a directory
     * succeeds silently — fread returns 0 and ftell may report 0 or
     * the block size, so a directory looks like a 0-byte file and the
     * hex grid renders just an offset column. This bit users who picked
     * Calculator.app from the Open dialog (before we set
     * treatsFilePackagesAsDirectories) and also covers drag-drop of a
     * folder onto an existing tab and CLI args pointing at a directory.
     * Use std::filesystem rather than stat() so the path-encoding
     * round-trip matches what the rest of the codebase uses. */
    {
        std::error_code ec;
        auto fsp    = PathFromUtf8(filename);
        auto status = std::filesystem::status(fsp, ec);
        if (!ec && std::filesystem::is_directory(status)) {
            /* Tailor the hint: for macOS bundle suffixes the actionable
             * advice is "open the package and pick a file inside";
             * for plain folders that's confusing and the right advice
             * is just "this is a folder, pick a file". Match by file
             * extension rather than HFS bundle bit so the hint works
             * on every platform (Win/Linux drag-drop of a .app
             * symlink, etc.). */
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
    }

    if (is_file_held_by_other_process(state_.filename)) {
        throw std::runtime_error(
            "'" + filename + "' is open in another program. "
            "Close it there first, then try again.");
    }

    /* Close the probe handle immediately — Windows' share check is
     * symmetric; holding write access would block external tools' saves. */
    FILE* probe = open_file_shared(state_.filename, "rb+");
    if (probe != nullptr) {
        fclose(probe);
        state_.is_readonly = 0;
    } else {
        state_.is_readonly = 1;
    }

    /* Long-lived handle is read-only; edits use transient write handles. */
    state_.fp = open_file_shared(state_.filename, "rb");
    if (state_.fp == nullptr) {
        throw std::runtime_error("Cannot open '" + filename + "'");
    }

    /* Disable stdio buffering on the read handle. Without this, EditByte
     * (which writes through a *separate* FILE* — see replace_byte_at_path
     * in fileops.cpp) updates the on-disk byte, but our long-lived stdio
     * buffer can serve stale data on the next ReadAt: macOS libc keeps
     * the read buffer valid across fseek when the new position lands
     * within the previously-buffered region (POSIX leaves this
     * implementation-defined), so the hex grid renders the pre-edit
     * value forever even though the file on disk is correct and the
     * status bar reports a successful edit. Windows stdio happens to
     * invalidate aggressively enough that the bug doesn't manifest
     * there, but enabling _IONBF unconditionally costs nothing —
     * ReadAt reads at most a few hundred bytes per frame and Search
     * reads in 4 KB chunks (larger than BUFSIZ), so the buffered
     * version was already doing one read() syscall per call.
     *
     * Check the return: setvbuf can fail (rare, but possible if some
     * libc impl flagged the FILE* as buffering-locked between fopen
     * and here). A silent fallback to default buffering would resurrect
     * the macOS staleness bug; surface it instead. */
    if (setvbuf(state_.fp, nullptr, _IONBF, 0) != 0) {
        fclose(state_.fp);
        state_.fp = nullptr;
        throw std::runtime_error(
            "Cannot disable buffering on '" + filename + "' (setvbuf failed); "
            "refusing to open with default buffering on macOS — would render "
            "stale bytes after edits.");
    }

    /* get_file_size does fseek-to-end / ftell / fseek-to-start on the
     * long-lived read handle. Search() and ReadAt() both use the same
     * handle and will fail or return wrong bytes if a concurrent caller
     * is mid-seek when this runs. All three live on the main UI thread
     * (the only place that touches state_.fp), so the round-trip here
     * is fine; flagged so a future move to a worker-pooled read path
     * doesn't quietly inherit the assumption. */
    state_.file_size = get_file_size(state_.fp);
    if (state_.file_size < 0) {
        fclose(state_.fp);
        state_.fp = nullptr;
        throw std::runtime_error("Cannot determine file size");
    }

    /* -1 means stat failed; degrade to "no detection" rather than fail
     * the open, since the file itself is readable. */
    baseline_token_ = get_file_mtime_token(state_.filename);
}

HexEditorCore::~HexEditorCore()
{
    if (state_.fp != nullptr)
        fclose(state_.fp);
}

std::vector<unsigned char> HexEditorCore::ReadAt(int64_t offset, size_t count) const
{
    if (offset < 0 || offset >= state_.file_size || count == 0)
        return {};

    int64_t avail = state_.file_size - offset;
    if ((int64_t)count > avail) count = (size_t)avail;

    std::vector<unsigned char> buf(count);
    if (fseek64(state_.fp, offset, SEEK_SET) != 0)
        return {};

    size_t got = fread(buf.data(), 1, count, state_.fp);
    buf.resize(got);
    return buf;
}

std::optional<EditResult> HexEditorCore::EditByte(int64_t offset, unsigned char new_val)
{
    if (state_.is_readonly)
        return std::nullopt;

    if (offset < 0 || offset >= state_.file_size)
        return std::nullopt;

    /* Atomic read+write under one handle, with a one-byte exclusive lock
     * on Windows so the undo stack always records the byte that was
     * actually replaced. The previous code read the old byte through the
     * long-lived read handle and then opened a separate write handle —
     * a concurrent external writer could mutate the byte between those
     * two calls, leaving the undo stack with a stale `old_val`. */
    unsigned char old_val = 0;
    if (replace_byte_at_path(state_.filename, offset, new_val, &old_val) != 0)
        return std::nullopt;

    undo_push(&state_, offset, old_val, new_val);

    /* Rebaseline so our own write doesn't trip HasExternalModification. */
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

    int64_t result = search_bytes(state_.fp, state_.file_size,
                                  start_offset, pattern.data(),
                                  static_cast<int>(pattern.size()));

    if (result == -1 && start_offset > 0) {
        result = search_bytes(state_.fp, state_.file_size, 0,
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

bool HexEditorCore::ReloadFromDisk()
{
    if (state_.fp == nullptr) return false;

    int64_t new_size = get_file_size(state_.fp);
    if (new_size < 0) return false;
    state_.file_size = new_size;

    /* Drop undo — old offsets may no longer refer to the same bytes. */
    state_.undo_count = 0;
    state_.undo_head  = 0;

    baseline_token_ = get_file_mtime_token(state_.filename);
    return true;
}
