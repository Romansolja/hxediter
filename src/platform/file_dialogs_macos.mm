/* NSOpenPanel-backed implementation of platform::OpenFileDialog and
 * platform::PickFolderDialog.
 *
 * Thread-safety: NSOpenPanel must run on the main thread. GLFW's macOS
 * event loop is on the process main thread (= AppKit main thread), so
 * the ImGui button callbacks that invoke these functions are already on
 * the right thread — no GCD dispatch needed. */

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
            /* Older macOS shows the title in the window chrome; newer
             * versions hide it but the prompt below the file list still
             * works as a heading. Setting both keeps the dialog readable
             * everywhere. */
            panel.message = @(title);
        }
        panel.canChooseFiles            = can_choose_files       ? YES : NO;
        panel.canChooseDirectories      = can_choose_directories ? YES : NO;
        panel.allowsMultipleSelection   = NO;
        panel.resolvesAliases           = YES;
        panel.canCreateDirectories      = can_choose_directories ? YES : NO;
        /* Let the user navigate INTO .app/.bundle/.framework directories
         * when picking a file. Without this NSOpenPanel treats packages
         * as opaque single entities — picking Calculator.app returns the
         * .app *directory* path, fopen("rb") opens the directory, ftell
         * returns 0 (or block size), and the hex grid renders the offset
         * column with no body. Hex-editor users want to inspect the
         * Mach-O inside the bundle (Contents/MacOS/<exe>), so opening
         * packages up is the right default. PickFolderDialog leaves it
         * off — picking a "folder" should still treat .app as one
         * entity since that's how the user thinks about it. */
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

} /* anonymous namespace */

std::optional<std::string> OpenFileDialog(const char* title) {
    return RunPanel(title, /*can_choose_files=*/true,
                           /*can_choose_directories=*/false);
}

std::optional<std::string> PickFolderDialog(const char* title) {
    return RunPanel(title, /*can_choose_files=*/false,
                           /*can_choose_directories=*/true);
}

} /* namespace platform */

#endif /* __APPLE__ */
