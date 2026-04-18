#ifdef _WIN32

#include "win_drop_target.h"

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

/* Extract the first path from a CF_HDROP payload. Returns empty string on
 * any failure (not an HDROP, no files, stat failed). */
static std::wstring FirstPathFromData(IDataObject* data) {
    FORMATETC fmt = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM stg = {};
    if (FAILED(data->GetData(&fmt, &stg))) return std::wstring();

    std::wstring result;
    HDROP hdrop = static_cast<HDROP>(GlobalLock(stg.hGlobal));
    if (hdrop) {
        UINT count = DragQueryFileW(hdrop, 0xFFFFFFFF, nullptr, 0);
        if (count > 0) {
            UINT len = DragQueryFileW(hdrop, 0, nullptr, 0);
            if (len > 0) {
                result.resize(len);
                /* DragQueryFile wants a buffer of len+1 to hold the
                 * terminator; std::wstring guarantees a writable extra
                 * null past data()+size(). */
                DragQueryFileW(hdrop, 0, result.data(), len + 1);
            }
        }
        GlobalUnlock(stg.hGlobal);
    }
    ReleaseStgMedium(&stg);
    return result;
}

static bool IsAcceptablePath(const std::wstring& path) {
    if (path.empty()) return false;
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) return false;
    return true;
}

WinDropTarget::WinDropTarget(DragState* drag_state, std::string* pending_path)
    : ref_count_(1),
      drag_state_(drag_state),
      pending_path_(pending_path),
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
    std::wstring first = FirstPathFromData(data);
    bool ok = IsAcceptablePath(first);
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
    std::wstring first = FirstPathFromData(data);
    bool ok = IsAcceptablePath(first);
    if (ok && pending_path_) {
        *pending_path_ = WideToUtf8(first.c_str());
    }
    if (drag_state_) *drag_state_ = DragState::None;
    cached_effect_ = DROPEFFECT_NONE;
    if (effect) *effect = ok ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    return S_OK;
}

} /* namespace platform */

#endif /* _WIN32 */
