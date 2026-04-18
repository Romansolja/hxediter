#pragma once

#include "editor.h"
#include <string>
#include <vector>
#include <cstdint>
#include <optional>

struct SearchResult {
    int64_t offset;
    int64_t page_offset;
};

struct EditResult {
    int64_t offset;
    unsigned char old_val;
    unsigned char new_val;
};

struct UndoResult {
    int64_t offset;
    unsigned char restored_val;
    unsigned char undone_val;
    int remaining_undos;
};

class HexEditorCore {
public:
    explicit HexEditorCore(const std::string& filename);
    ~HexEditorCore();

    HexEditorCore(const HexEditorCore&) = delete;
    HexEditorCore& operator=(const HexEditorCore&) = delete;

    std::vector<unsigned char> GetPageData() const;

    int64_t GetCurrentOffset() const;
    bool PageNext();
    bool PagePrev();
    bool GoToOffset(int64_t offset);

    std::optional<EditResult> EditByte(int64_t offset, unsigned char new_val);
    std::optional<UndoResult> Undo();

    /* Wraps around to the beginning if not found searching forward. */
    std::optional<SearchResult> Search(const std::vector<unsigned char>& pattern);

    int64_t     GetFileSize() const;
    std::string GetFilename() const;
    bool        IsReadOnly() const;
    int64_t     GetPageNumber() const;
    int64_t     GetTotalPages() const;
    int         GetUndoCount() const;

    /* True if the file on disk differs from the baseline captured at
     * open (or after our last successful edit/reload). */
    bool HasExternalModification() const;

    /* Rebaselines and drops pending undo. Used by the conflict dialog's
     * "Reload from disk" path. Returns false if the file handle is bad. */
    bool ReloadFromDisk();

private:
    EditorState state_;
    std::string filename_storage_;
    int64_t     baseline_token_ = -1;
};
