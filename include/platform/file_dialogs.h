#pragma once

#include <optional>
#include <string>

namespace platform {

/* Show the OS-native "Open File" dialog and block until the user picks a
 * file or cancels. `parent_native_handle` is the HWND of the main window
 * on Windows (cast from glfwGetWin32Window); ignored on platforms where
 * the dialog system doesn't take a parent handle. `title` is the dialog
 * caption.
 *
 * Returns the selected absolute path as UTF-8, or std::nullopt if the
 * user cancelled (or the dialog couldn't be created). */
std::optional<std::string> OpenFileDialog(void* parent_native_handle,
                                          const char* title);

/* Same as OpenFileDialog but for picking a directory. The returned path
 * is the folder itself, UTF-8 encoded. */
std::optional<std::string> PickFolderDialog(void* parent_native_handle,
                                            const char* title);

} /* namespace platform */
