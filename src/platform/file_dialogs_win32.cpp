#ifdef _WIN32

#include "platform/file_dialogs.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>

namespace platform {

namespace {

/* Minimal RAII for IUnknown-derived pointers. Avoids leaking dialog/item
 * COM objects on any of the early-return paths below. */
template <typename T>
struct ComPtr {
    T* p = nullptr;
    ComPtr() = default;
    ~ComPtr() { if (p) p->Release(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    T* get() const { return p; }
};

static std::wstring Utf8ToWide(const char* s) {
    if (!s || !*s) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (n <= 1) return std::wstring();
    std::wstring out(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), n);
    return out;
}

static std::string WideToUtf8(const wchar_t* w) {
    if (!w) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return std::string();
    std::string out(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
    return out;
}

/* Common driver: CoCreateInstance the dialog, set title and options, show
 * modally parented to the caller's HWND, and extract SIGDN_FILESYSPATH on
 * success. `extra_opts` is OR'd into the FILEOPENDIALOGOPTIONS — the only
 * mode-specific bit (FOS_PICKFOLDERS) flows through it.
 *
 * Relies on main.cpp's OleInitialize for the apartment-threaded COM
 * apartment that IFileOpenDialog requires; both calls are on the UI
 * thread so there's no cross-apartment marshalling. */
static std::optional<std::string> ShowFileOpen(void* parent_native_handle,
                                               const char* title,
                                               FILEOPENDIALOGOPTIONS extra_opts,
                                               const COMDLG_FILTERSPEC* filters,
                                               UINT filter_count) {
    ComPtr<IFileOpenDialog> dlg;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
    if (FAILED(hr) || !dlg.get()) return std::nullopt;

    /* FOS_NOCHANGEDIR keeps the process CWD pinned — without it the dialog
     * leaves us wherever the user navigated, breaking later relative
     * filesystem ops. FORCEFILESYSTEM rejects shell-only items (Recycle
     * Bin, This PC) so GetDisplayName(SIGDN_FILESYSPATH) always succeeds. */
    FILEOPENDIALOGOPTIONS opts = 0;
    if (FAILED(dlg->GetOptions(&opts))) opts = 0;
    opts |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
    opts |= extra_opts;
    dlg->SetOptions(opts);

    if (title && *title) {
        std::wstring wtitle = Utf8ToWide(title);
        if (!wtitle.empty()) dlg->SetTitle(wtitle.c_str());
    }

    if (filters && filter_count > 0) {
        dlg->SetFileTypes(filter_count, filters);
        dlg->SetFileTypeIndex(1);
    }

    HWND parent = static_cast<HWND>(parent_native_handle);
    hr = dlg->Show(parent);
    /* User clicked Cancel / pressed Esc — not an error, just no selection. */
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return std::nullopt;
    if (FAILED(hr)) return std::nullopt;

    ComPtr<IShellItem> item;
    if (FAILED(dlg->GetResult(&item)) || !item.get()) return std::nullopt;

    PWSTR wpath = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &wpath)) || !wpath) {
        return std::nullopt;
    }
    std::string utf8 = WideToUtf8(wpath);
    CoTaskMemFree(wpath);
    if (utf8.empty()) return std::nullopt;
    return utf8;
}

} /* anonymous namespace */

std::optional<std::string> OpenFileDialog(void* parent_native_handle,
                                          const char* title) {
    /* Single "All files" filter mirrors the previous ImGuiFileDialog
     * behaviour (filter == ".*"). */
    const COMDLG_FILTERSPEC filters[] = {
        { L"All files", L"*.*" },
    };
    return ShowFileOpen(parent_native_handle, title,
                        FOS_FILEMUSTEXIST,
                        filters, 1);
}

std::optional<std::string> PickFolderDialog(void* parent_native_handle,
                                            const char* title) {
    return ShowFileOpen(parent_native_handle, title,
                        FOS_PICKFOLDERS,
                        nullptr, 0);
}

} /* namespace platform */

#endif /* _WIN32 */
