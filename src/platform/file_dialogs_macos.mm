// NSOpenPanel needs the main thread — GLFW's macOS loop is on AppKit's, so no GCD dispatch.

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
            // Set both — older macOS shows the title chrome, newer macOS hides it but shows message.
            panel.message = @(title);
        }
        panel.canChooseFiles            = can_choose_files       ? YES : NO;
        panel.canChooseDirectories      = can_choose_directories ? YES : NO;
        panel.allowsMultipleSelection   = NO;
        panel.resolvesAliases           = YES;
        panel.canCreateDirectories      = can_choose_directories ? YES : NO;
        // File-pick: descend into .app/.bundle/.framework so users can hit the inner Mach-O.
        // Folder-pick: leave off — a folder pick should treat .app as one entity.
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

}

std::optional<std::string> OpenFileDialog(const char* title) {
    return RunPanel(title, /*can_choose_files=*/true,
                           /*can_choose_directories=*/false);
}

std::optional<std::string> PickFolderDialog(const char* title) {
    return RunPanel(title, /*can_choose_files=*/false,
                           /*can_choose_directories=*/true);
}

}

#endif
