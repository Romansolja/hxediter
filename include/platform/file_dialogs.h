#pragma once

#include <optional>
#include <string>

namespace platform {

/* Show NSOpenPanel and block until the user picks a file or cancels.
 * `title` is the dialog caption.
 *
 * Returns the selected absolute path as UTF-8, or std::nullopt if the
 * user cancelled (or the dialog couldn't be created). */
std::optional<std::string> OpenFileDialog(const char* title);

/* Same as OpenFileDialog but for picking a directory. The returned path
 * is the folder itself, UTF-8 encoded. */
std::optional<std::string> PickFolderDialog(const char* title);

} /* namespace platform */
