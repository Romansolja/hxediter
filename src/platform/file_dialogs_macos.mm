// NSOpenPanel-backed platform::OpenFileDialog and PickFolderDialog.
// NSOpenPanel must run on the main thread; GLFW's macOS event loop runs
// on the AppKit main thread, so ImGui button callbacks are already there.
// No GCD dispatch needed.

#ifdef __APPLE__

#include "platform/file_dialogs.h"

#import <AppKit/AppKit.h>

namespace platform {

namespace {

std::optional<std::string> RunPanel(const char* title,
                                    bool can_choose_files,
                                    bool can_choose_directories) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        if (title && *title) {
            panel.title   = @(title);
            // Older macOS shows title in window chrome; newer versions
            // hide it but the message below the file list still acts as
            // a heading. Setting both keeps the dialog readable everywhere.
            panel.message = @(title);
        }
        panel.canChooseFiles            = can_choose_files       ? YES : NO;
        panel.canChooseDirectories      = can_choose_directories ? YES : NO;
        panel.allowsMultipleSelection   = NO;
        panel.resolvesAliases           = YES;
        panel.canCreateDirectories      = can_choose_directories ? YES : NO;
        // Let the user navigate INTO .app/.bundle/.framework directories
        // when picking a file. Otherwise NSOpenPanel treats packages as
        // opaque entities and returns the .app *directory* — hex-editor
        // users want to inspect the Mach-O inside (Contents/MacOS/<exe>).
        // PickFolderDialog leaves it off — a "folder" should treat .app
        // as one entity, matching the user's mental model.
        if (can_choose_files && !can_choose_directories) {
            panel.treatsFilePackagesAsDirectories = YES;
        }

        NSModalResponse rc = [panel runModal];
        if (rc != NSModalResponseOK) return std::nullopt;

        NSURL* url = panel.URL;
        if (url == nil) return std::nullopt;

        const char* utf8 = url.fileSystemRepresentation;
        if (utf8 == nullptr) return std::nullopt;
        return std::string(utf8);
    }
}

} // anonymous namespace

std::optional<std::string> OpenFileDialog(const char* title) {
    return RunPanel(title, /*can_choose_files=*/true,
                           /*can_choose_directories=*/false);
}

std::optional<std::string> PickFolderDialog(const char* title) {
    return RunPanel(title, /*can_choose_files=*/false,
                           /*can_choose_directories=*/true);
}

} // namespace platform

#endif // __APPLE__
