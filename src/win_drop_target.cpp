#ifdef _WIN32

#include "win_drop_target.h"
#include "path_utils.h"

#include <shellapi.h>

namespace platform {

static std::string WideToUtf8(const wchar_t* w) {
    if (!w) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return std::string();
    std::string out(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
    return out;
}

/* RAII wrapper for the CF_HDROP STGMEDIUM + GlobalLock. Without it, an
 * exception thrown inside the locked region (e.g. bad_alloc from vector
 * growth) would leak the OLE handle even though the outer try/catch in
 * Drop() keeps the process alive. */
namespace {
struct HDropLock {
    STGMEDIUM stg{};
    HDROP     hdrop  = nullptr;
    bool      have_data = false;

    explicit HDropLock(IDataObject* data) {
        FORMATETC fmt = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        if (FAILED(data->GetData(&fmt, &stg))) return;
        have_data = true;
        hdrop = static_cast<HDROP>(GlobalLock(stg.hGlobal));
    }

    ~HDropLock() {
        if (hdrop)     GlobalUnlock(stg.hGlobal);
        if (have_data) ReleaseStgMedium(&stg);
    }

    HDropLock(const HDropLock&) = delete;
    HDropLock& operator=(const HDropLock&) = delete;
};
} /* anonymous namespace */

/* All paths from a CF_HDROP payload; empty on any failure. */
static std::vector<std::wstring> AllPathsFromData(IDataObject* data) {
    std::vector<std::wstring> results;
    HDropLock lock(data);
    if (!lock.hdrop) return results;

    UINT count = DragQueryFileW(lock.hdrop, 0xFFFFFFFF, nullptr, 0);
    results.reserve(count);
    for (UINT i = 0; i < count; ++i) {
        UINT len = DragQueryFileW(lock.hdrop, i, nullptr, 0);
        if (len == 0) continue;
        std::wstring w;
        w.resize(len);
        /* DragQueryFile wants len+1 bytes for the terminator. */
        DragQueryFileW(lock.hdrop, i, w.data(), len + 1);
        results.push_back(std::move(w));
    }
    return results;
}

/* Accept both files and existing directories; fail only on missing /
 * unreadable entries. */
static bool IsAcceptablePath(const std::wstring& path) {
    if (path.empty()) return false;
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    return true;
}

static bool IsDirectory(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

WinDropTarget::WinDropTarget(DragState* drag_state,
                             std::vector<std::string>* pending_paths,
                             std::vector<std::string>* pending_directories)
    : ref_count_(1),
      drag_state_(drag_state),
      pending_paths_(pending_paths),
      pending_directories_(pending_directories),
      cached_effect_(DROPEFFECT_NONE) {}

HRESULT STDMETHODCALLTYPE WinDropTarget::QueryInterface(REFIID riid, void** out) {
    if (!out) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
        *out = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE WinDropTarget::AddRef() {
    return InterlockedIncrement(&ref_count_);
}

ULONG STDMETHODCALLTYPE WinDropTarget::Release() {
    LONG n = InterlockedDecrement(&ref_count_);
    if (n == 0) delete this;
    return static_cast<ULONG>(n);
}

HRESULT STDMETHODCALLTYPE WinDropTarget::DragEnter(IDataObject* data,
                                                   DWORD /*key_state*/,
                                                   POINTL /*pt*/,
                                                   DWORD* effect) {
    /* Validate every path so the green/red overlay doesn't lie about a
     * mixed drop. This is GetFileAttributesW only — no I/O on file
     * contents — so even a hundreds-of-paths drop stays cheap. */
    std::vector<std::wstring> paths = AllPathsFromData(data);
    bool ok = !paths.empty();
    for (const auto& p : paths) {
        if (!IsAcceptablePath(p)) { ok = false; break; }
    }
    if (drag_state_) *drag_state_ = ok ? DragState::Valid : DragState::Invalid;
    cached_effect_ = ok ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    if (effect) *effect = cached_effect_;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WinDropTarget::DragOver(DWORD /*key_state*/,
                                                  POINTL /*pt*/,
                                                  DWORD* effect) {
    if (effect) *effect = cached_effect_;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WinDropTarget::DragLeave() {
    if (drag_state_) *drag_state_ = DragState::None;
    cached_effect_ = DROPEFFECT_NONE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WinDropTarget::Drop(IDataObject* data,
                                              DWORD /*key_state*/,
                                              POINTL /*pt*/,
                                              DWORD* effect) {
    bool any_ok = false;
    /* Catch everything: a C++ exception escaping a COM callback aborts the
     * process. bad_alloc from a directory walk full of paths is the
     * realistic case; we'd rather drop the batch than crash. */
    try {
        std::vector<std::wstring> paths = AllPathsFromData(data);
        for (const auto& w : paths) {
            if (!IsAcceptablePath(w)) continue;
            any_ok = true;
            if (IsDirectory(w)) {
                /* Folders go to a separate queue so the main loop can
                 * populate the directory dropdown without auto-opening
                 * every file as a tab. Falls back to expansion-into-tabs
                 * only if no directory queue was wired up. */
                if (pending_directories_) {
                    pending_directories_->push_back(WideToUtf8(w.c_str()));
                } else if (pending_paths_) {
                    ExpandDirectoryInto(std::filesystem::path(w),
                                        *pending_paths_);
                }
            } else if (pending_paths_) {
                pending_paths_->push_back(WideToUtf8(w.c_str()));
            }
        }
    } catch (...) {
        /* Silently swallow — main loop will simply see no new paths and the
         * user can retry with a smaller selection. */
    }
    if (drag_state_) *drag_state_ = DragState::None;
    cached_effect_ = DROPEFFECT_NONE;
    if (effect) *effect = any_ok ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    return S_OK;
}

} /* namespace platform */

#endif /* _WIN32 */
