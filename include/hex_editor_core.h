/* hex_editor_core.h — Core hex editor logic (Model), no display dependencies */

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

    /* Page data — returns raw bytes for the current page */
    std::vector<unsigned char> GetPageData() const;

    /* Navigation */
    int64_t GetCurrentOffset() const;
    bool PageNext();
    bool PagePrev();
    bool GoToOffset(int64_t offset);

    /* Editing */
    std::optional<EditResult> EditByte(int64_t offset, unsigned char new_val);

    /* Undo */
    std::optional<UndoResult> Undo();

    /* Search — wraps around if not found forward */
    std::optional<SearchResult> Search(const std::vector<unsigned char>& pattern);

    /* Read-only accessors */
    int64_t     GetFileSize() const;
    std::string GetFilename() const;
    bool        IsReadOnly() const;
    int64_t     GetPageNumber() const;
    int64_t     GetTotalPages() const;
    int         GetUndoCount() const;

    /* Returns true if the file on disk has a different mtime/size than
     * the baseline captured at open (or after the last successful edit or
     * reload). This is how the UI detects "another program modified the
     * file while we have it open". */
    bool HasExternalModification() const;

    /* Re-reads the current page from disk, re-captures the mtime/size
     * baseline, and drops any pending undo entries. Safe to call any time;
     * used by the conflict dialog's "Reload from disk" path. Returns false
     * if the file handle is bad (shouldn't happen in normal flow). */
    bool ReloadFromDisk();

private:
    EditorState state_;
    std::string filename_storage_;
    int64_t     baseline_token_ = -1;  /* mtime+size token from fileops */
};
