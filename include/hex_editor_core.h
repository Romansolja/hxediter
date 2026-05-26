#pragma once

#include "editor.h"
#include <string>
#include <vector>
#include <cstdint>
#include <optional>

struct SearchResult {
    int64_t offset;
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

    // Up to `count` bytes from `offset` (clamped to EOF). Empty on failure.
    // The file is never resident in RAM — only visible rows are pulled per frame.
    std::vector<unsigned char> ReadAt(int64_t offset, size_t count) const;

    std::optional<EditResult> EditByte(int64_t offset, unsigned char new_val);
    std::optional<UndoResult> Undo();

    // Wraps to the beginning if no forward match.
    std::optional<SearchResult> Search(const std::vector<unsigned char>& pattern,
                                        int64_t start_offset);

    int64_t     GetFileSize() const;
    std::string GetFilename() const;
    bool        IsReadOnly() const;
    // One-way latch — flips is_readonly on regardless of filesystem permissions.
    void        ForceReadOnly();
    int         GetUndoCount() const;

    // True if the file on disk differs from the baseline at open or last write.
    bool HasExternalModification() const;

    // Resync baseline_token_ with disk without touching bytes or undo.
    void Rebaseline();

    // Rebaseline + drop pending undo. False if the file handle is bad.
    bool ReloadFromDisk();

private:
    EditorState state_;
    std::string filename_storage_;
    int64_t     baseline_token_ = -1;
};
