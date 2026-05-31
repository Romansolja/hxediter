#pragma once

#include "editor.h"
#include "file_handle.h"
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

    // Re-probe the on-disk size from the open read handle, leaving bytes and
    // undo untouched. For the "Keep my edits" path, which accepts an external
    // resize without reopening.
    void RefreshFileSize();

    // Rebaseline + drop pending undo. False if the file handle is bad.
    bool ReloadFromDisk();

private:
    struct OpenedFile {
        FileHandle fp;
        int64_t    size;
        int64_t    block_size = 0;   // >0 for a device (read alignment); 0 for a regular file
    };
    // Opens "rb" with _IONBF, probes size. Throws on failure when
    // `throw_on_failure` is true (constructor path); returns std::nullopt
    // when false (ReloadFromDisk path). The filename in the error message
    // comes from filename_storage_.
    std::optional<OpenedFile> OpenAndProbe(bool throw_on_failure);

    // First match of `pattern` at/after `start`, or -1. Dispatches to the raw
    // fread scan (regular files) or the block-aligned ReadAt scan (devices).
    int64_t SearchFrom(const std::vector<unsigned char>& pattern, int64_t start) const;
    // Device search reads via ReadAt (block-aligned) — a raw /dev/rdiskN rejects
    // the unaligned reads search_bytes would otherwise issue.
    int64_t SearchDevice(const std::vector<unsigned char>& pattern, int64_t start) const;

    EditorState state_;
    FileHandle  fp_;                       // read handle — UI-thread-only
    std::string filename_storage_;
    int64_t     baseline_token_ = -1;
    bool        forced_readonly_ = false;  // ForceReadOnly latched on
    bool        is_device_ = false;        // block/character device opened for inspection
    int64_t     device_block_size_ = 0;    // read alignment when is_device_; 0 for regular files
};
