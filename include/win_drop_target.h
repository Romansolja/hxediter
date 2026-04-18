#pragma once

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleidl.h>

#include <string>

namespace platform {

/* Shared drag/drop state flipped by the COM IDropTarget callbacks and
 * read by the render thread each frame. All callbacks fire on the UI
 * thread during glfwPollEvents, so plain scalar access is safe. */
enum class DragState : int { None = 0, Valid = 1, Invalid = 2 };

class WinDropTarget : public IDropTarget {
public:
    WinDropTarget(DragState* drag_state, std::string* pending_path);

    /* IUnknown */
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override;
    ULONG   STDMETHODCALLTYPE AddRef()  override;
    ULONG   STDMETHODCALLTYPE Release() override;

    /* IDropTarget */
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD key_state,
                                        POINTL pt, DWORD* effect) override;
    HRESULT STDMETHODCALLTYPE DragOver (DWORD key_state, POINTL pt,
                                        DWORD* effect) override;
    HRESULT STDMETHODCALLTYPE DragLeave() override;
    HRESULT STDMETHODCALLTYPE Drop     (IDataObject* data, DWORD key_state,
                                        POINTL pt, DWORD* effect) override;

private:
    LONG        ref_count_;
    DragState*   drag_state_;     /* not owned */
    std::string* pending_path_;  /* not owned */
    DWORD       cached_effect_;  /* echoed from DragEnter in DragOver */
};

} /* namespace platform */

#endif /* _WIN32 */
