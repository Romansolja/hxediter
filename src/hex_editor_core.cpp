#include "hex_editor_core.h"
#include "fileops.h"
#include "undo.h"
#include <stdexcept>

HexEditorCore::HexEditorCore(const std::string& filename)
    : state_{}, filename_storage_(filename)
{
    state_.filename = filename_storage_.c_str();

    if (is_file_held_by_other_process(state_.filename)) {
        throw std::runtime_error(
            "'" + filename + "' is open in another program. "
            "Close it there first, then try again.");
    }

    /* Close the probe handle immediately: Windows' share check is
     * symmetric, so holding write access here would block external
     * tools' saves even with _SH_DENYNO. */
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

    state_.file_size = get_file_size(state_.fp);
    if (state_.file_size < 0) {
        fclose(state_.fp);
        state_.fp = nullptr;
        throw std::runtime_error("Cannot determine file size");
    }

    /* -1 means stat failed: degrade to "no detection" rather than
     * failing the open, since the file itself is readable. */
    baseline_token_ = get_file_mtime_token(state_.filename);
}

HexEditorCore::~HexEditorCore()
{
    if (state_.fp != nullptr)
        fclose(state_.fp);
}

std::vector<unsigned char> HexEditorCore::GetPageData() const
{
    std::vector<unsigned char> buf(PAGE_SIZE);

    if (fseek64(state_.fp, state_.page_offset, SEEK_SET) != 0)
        return {};

    size_t got = fread(buf.data(), 1, PAGE_SIZE, state_.fp);
    buf.resize(got);
    return buf;
}

int64_t HexEditorCore::GetCurrentOffset() const
{
    return state_.page_offset;
}

bool HexEditorCore::PageNext()
{
    if (state_.page_offset + PAGE_SIZE < state_.file_size) {
        state_.page_offset += PAGE_SIZE;
        return true;
    }
    return false;
}

bool HexEditorCore::PagePrev()
{
    if (state_.page_offset >= PAGE_SIZE) {
        state_.page_offset -= PAGE_SIZE;
        return true;
    }
    if (state_.page_offset > 0) {
        state_.page_offset = 0;
        return true;
    }
    return false;
}

bool HexEditorCore::GoToOffset(int64_t offset)
{
    if (offset < 0 || offset >= state_.file_size)
        return false;

    state_.page_offset = (offset / PAGE_SIZE) * PAGE_SIZE;
    return true;
}

std::optional<EditResult> HexEditorCore::EditByte(int64_t offset, unsigned char new_val)
{
    if (state_.is_readonly)
        return std::nullopt;

    if (offset < 0 || offset >= state_.file_size)
        return std::nullopt;

    if (fseek64(state_.fp, offset, SEEK_SET) != 0)
        return std::nullopt;

    int old_ch = fgetc(state_.fp);
    if (old_ch == EOF)
        return std::nullopt;

    unsigned char old_val = static_cast<unsigned char>(old_ch);

    if (write_byte_at_path(state_.filename, offset, new_val) != 0)
        return std::nullopt;

    undo_push(&state_, offset, old_val, new_val);
    state_.page_offset = (offset / PAGE_SIZE) * PAGE_SIZE;

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

    state_.page_offset = (entry.offset / PAGE_SIZE) * PAGE_SIZE;
    baseline_token_ = get_file_mtime_token(state_.filename);

    return UndoResult{entry.offset, entry.old_val, entry.new_val, state_.undo_count};
}

std::optional<SearchResult> HexEditorCore::Search(const std::vector<unsigned char>& pattern)
{
    if (pattern.empty())
        return std::nullopt;

    int64_t result = search_bytes(state_.fp, state_.file_size,
                                  state_.page_offset, pattern.data(),
                                  static_cast<int>(pattern.size()));

    if (result == -1 && state_.page_offset > 0) {
        result = search_bytes(state_.fp, state_.file_size, 0,
                              pattern.data(), static_cast<int>(pattern.size()));
    }

    if (result == -1)
        return std::nullopt;

    state_.page_offset = (result / PAGE_SIZE) * PAGE_SIZE;
    return SearchResult{result, state_.page_offset};
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

int64_t HexEditorCore::GetPageNumber() const
{
    return (state_.page_offset / PAGE_SIZE) + 1;
}

int64_t HexEditorCore::GetTotalPages() const
{
    return (state_.file_size + PAGE_SIZE - 1) / PAGE_SIZE;
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

    /* Clamp page offset so the caret isn't past EOF after a truncation. */
    if (state_.page_offset >= state_.file_size) {
        state_.page_offset = (state_.file_size > 0)
            ? ((state_.file_size - 1) / PAGE_SIZE) * PAGE_SIZE
            : 0;
    }

    /* Drop undo — old offsets may no longer refer to the same bytes. */
    state_.undo_count = 0;
    state_.undo_head  = 0;

    baseline_token_ = get_file_mtime_token(state_.filename);
    return true;
}
