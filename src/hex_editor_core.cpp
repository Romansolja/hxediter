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

    // Reject directories up front. On macOS, fopen() on a directory
    // succeeds silently — a directory ends up looking like a 0-byte file
    // and the hex grid renders just an offset column. Bit users who
    // picked Calculator.app from the Open dialog (before we set
    // treatsFilePackagesAsDirectories); also covers drag-drop of a folder
    // and CLI args pointing at a directory.
    {
        std::error_code ec;
        auto fsp    = PathFromUtf8(filename);
        auto status = std::filesystem::status(fsp, ec);
        if (!ec && std::filesystem::is_directory(status)) {
            // Match by file extension rather than HFS bundle bit so the
            // hint works for cross-platform drag-drop of .app symlinks.
            // Bundle suffixes get the "open the package" hint; plain
            // folders just get "this is a folder".
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

    // Probe writability without holding the rb+ handle open — actual
    // edits go through transient handles in fileops.cpp.
    FILE* probe = open_file_shared(state_.filename, "rb+");
    if (probe != nullptr) {
        fclose(probe);
        state_.is_readonly = 0;
    } else {
        state_.is_readonly = 1;
    }

    // Long-lived handle is read-only; edits use transient write handles.
    state_.fp = open_file_shared(state_.filename, "rb");
    if (state_.fp == nullptr) {
        throw std::runtime_error("Cannot open '" + filename + "'");
    }

    // Disable stdio buffering on the read handle. EditByte writes through
    // a *separate* FILE* (see replace_byte_at_path); macOS libc keeps the
    // read buffer valid across fseek when the new position lands inside
    // the previously-buffered region (POSIX leaves this implementation-
    // defined), so ReadAt would serve stale bytes even though the on-disk
    // value is correct. _IONBF is free here: ReadAt pulls only visible
    // rows and Search reads in 4 KB chunks (> BUFSIZ), so the buffered
    // path was already one read() per call.
    //
    // If setvbuf fails (rare — buffering-locked FILE*), surface it rather
    // than silently falling back to default buffering and resurrecting
    // the staleness bug.
    if (setvbuf(state_.fp, nullptr, _IONBF, 0) != 0) {
        fclose(state_.fp);
        state_.fp = nullptr;
        throw std::runtime_error(
            "Cannot disable buffering on '" + filename + "' (setvbuf failed); "
            "refusing to open with default buffering on macOS — would render "
            "stale bytes after edits.");
    }

    // get_file_size seeks on state_.fp, which is also used by Search and
    // ReadAt. All three live on the main UI thread (the only place that
    // touches state_.fp); a future move to worker-pooled reads must not
    // quietly inherit this assumption.
    state_.file_size = get_file_size(state_.fp);
    if (state_.file_size < 0) {
        fclose(state_.fp);
        state_.fp = nullptr;
        throw std::runtime_error("Cannot determine file size");
    }

    // -1 means stat failed; degrade to "no detection" rather than fail
    // the open, since the file itself is readable.
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
    if (fseeko(state_.fp, offset, SEEK_SET) != 0)
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

    // Atomic read+write — see replace_byte_at_path in fileops.h.
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

void HexEditorCore::Rebaseline()
{
    baseline_token_ = get_file_mtime_token(state_.filename);
}

bool HexEditorCore::ReloadFromDisk()
{
    if (state_.fp == nullptr) return false;

    int64_t new_size = get_file_size(state_.fp);
    if (new_size < 0) return false;
    state_.file_size = new_size;

    // Drop undo — old offsets may no longer refer to the same bytes.
    state_.undo_count = 0;
    state_.undo_head  = 0;

    baseline_token_ = get_file_mtime_token(state_.filename);
    return true;
}
