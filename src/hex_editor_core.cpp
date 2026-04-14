/* hex_editor_core.cpp — Core hex editor logic implementation */

#include "hex_editor_core.h"
#include "fileops.h"
#include "undo.h"
#include <stdexcept>

HexEditorCore::HexEditorCore(const std::string& filename)
    : state_{}, filename_storage_(filename)
{
    state_.filename = filename_storage_.c_str();

    /* Try read+write first; fall back to read-only */
    state_.fp = fopen(state_.filename, "rb+");
    if (state_.fp == nullptr) {
        state_.fp = fopen(state_.filename, "rb");
        if (state_.fp == nullptr) {
            throw std::runtime_error("Cannot open '" + filename + "'");
        }
        state_.is_readonly = 1;
    }

    state_.file_size = get_file_size(state_.fp);
    if (state_.file_size < 0) {
        fclose(state_.fp);
        state_.fp = nullptr;
        throw std::runtime_error("Cannot determine file size");
    }
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

    /* Read existing byte for undo */
    if (fseek64(state_.fp, offset, SEEK_SET) != 0)
        return std::nullopt;

    int old_ch = fgetc(state_.fp);
    if (old_ch == EOF)
        return std::nullopt;

    unsigned char old_val = static_cast<unsigned char>(old_ch);

    if (write_byte_at(state_.fp, offset, new_val) != 0)
        return std::nullopt;

    undo_push(&state_, offset, old_val, new_val);
    state_.page_offset = (offset / PAGE_SIZE) * PAGE_SIZE;

    return EditResult{offset, old_val, new_val};
}

std::optional<UndoResult> HexEditorCore::Undo()
{
    UndoEntry entry;

    if (!undo_pop(&state_, &entry))
        return std::nullopt;

    if (write_byte_at(state_.fp, entry.offset, entry.old_val) != 0) {
        undo_unpop(&state_);
        return std::nullopt;
    }

    state_.page_offset = (entry.offset / PAGE_SIZE) * PAGE_SIZE;

    return UndoResult{entry.offset, entry.old_val, entry.new_val, state_.undo_count};
}

std::optional<SearchResult> HexEditorCore::Search(const std::vector<unsigned char>& pattern)
{
    if (pattern.empty())
        return std::nullopt;

    int64_t result = search_bytes(state_.fp, state_.file_size,
                                  state_.page_offset, pattern.data(),
                                  static_cast<int>(pattern.size()));

    /* Wrap around to beginning if not found */
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
