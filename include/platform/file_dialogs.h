#pragma once

#include <optional>
#include <string>

namespace platform {

// Blocking native picker. UTF-8 path on OK, nullopt on cancel/error.
std::optional<std::string> OpenFileDialog(const char* title);
std::optional<std::string> PickFolderDialog(const char* title);

}
