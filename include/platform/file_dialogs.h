#pragma once

#include <optional>
#include <string>

namespace platform {

// Show NSOpenPanel and block until the user picks a file or cancels.
// Returns the selected absolute path as UTF-8, or std::nullopt on cancel
// (or if the dialog couldn't be created).
std::optional<std::string> OpenFileDialog(const char* title);

// Same as OpenFileDialog but for picking a directory. Returned path is
// the folder itself, UTF-8 encoded.
std::optional<std::string> PickFolderDialog(const char* title);

} // namespace platform
