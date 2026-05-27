// Routes Finder's "Open With → hxediter" into the same pending_paths queue that
// drag-drop uses. Without this handler the kAEOpenDocuments AppleEvent goes to
// NSApplication's default no-op handler — GLFW never surfaces it — and the
// Finder menu entry registered via CFBundleDocumentTypes appears to do nothing.

#ifdef __APPLE__

#include "platform/macos_apple_events.h"

#include <filesystem>
#include <mutex>
#include <system_error>

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>

namespace {

// Same-thread today (NSAppleEventManager dispatches on the main thread, the
// drain runs on the main thread), but the mutex is free insurance against a
// future move off the main thread.
std::mutex g_mutex;
std::vector<std::string> g_pending_paths;
std::vector<std::string> g_pending_directories;

}

// Internal — keeps Objective-C out of the header. The selector signature is
// fixed by NSAppleEventManager's setEventHandler:andSelector:... API; there's
// no block-form variant to switch to.
@interface HxEdAppleEventTarget : NSObject
- (void)handleOpenDocuments:(NSAppleEventDescriptor*)event
             withReplyEvent:(NSAppleEventDescriptor*)reply;
@end

@implementation HxEdAppleEventTarget

- (void)handleOpenDocuments:(NSAppleEventDescriptor*)event
             withReplyEvent:(NSAppleEventDescriptor*)reply {
    (void)reply;
    NSAppleEventDescriptor* list =
        [event paramDescriptorForKeyword:keyDirectObject];
    if (list == nil) return;

    NSInteger count = [list numberOfItems];
    std::vector<std::string> files;
    std::vector<std::string> dirs;
    files.reserve((size_t)count);

    // AppleEvent lists are 1-indexed.
    for (NSInteger i = 1; i <= count; ++i) {
        NSAppleEventDescriptor* item = [list descriptorAtIndex:i];
        if (item == nil) continue;

        // Older senders may pass typeAlias or typeFSRef — coerce to typeFileURL
        // so we get a stable URL-bytes representation regardless of source.
        NSAppleEventDescriptor* urlDesc =
            [item coerceToDescriptorType:typeFileURL];
        if (urlDesc == nil) continue;

        NSData* bytes = [urlDesc data];
        if (bytes == nil || bytes.length == 0) continue;

        NSURL* url = [NSURL URLWithDataRepresentation:bytes relativeToURL:nil];
        if (url == nil) continue;

        const char* utf8 = url.fileSystemRepresentation;
        if (utf8 == nullptr) continue;

        std::string path(utf8);
        std::error_code ec;
        auto status = std::filesystem::status(
            std::filesystem::path(path), ec);
        if (ec) continue;

        // Mirror glfw_drop_callback's split so directories don't sneak into the
        // file-batch loop. Today CFBundleDocumentTypes only declares public.data
        // so Finder won't actually route folders here — but the parallel
        // structure means a future Info.plist tweak can't silently misroute.
        if (std::filesystem::is_directory(status)) {
            dirs.push_back(std::move(path));
        } else if (std::filesystem::is_regular_file(status)) {
            files.push_back(std::move(path));
        }
    }

    if (files.empty() && dirs.empty()) return;

    std::lock_guard<std::mutex> lock(g_mutex);
    g_pending_paths.insert(g_pending_paths.end(),
        std::make_move_iterator(files.begin()),
        std::make_move_iterator(files.end()));
    g_pending_directories.insert(g_pending_directories.end(),
        std::make_move_iterator(dirs.begin()),
        std::make_move_iterator(dirs.end()));
}

@end

namespace platform {

void RegisterAppleEventHandlers() {
    @autoreleasepool {
        // NSAppleEventManager holds the target by unsafe-unretained reference —
        // a stack-local would dangle on the first event. Static storage gives
        // it program lifetime; registering twice is a no-op (the manager just
        // replaces the prior binding for the same event class/id).
        static HxEdAppleEventTarget* target = [[HxEdAppleEventTarget alloc] init];

        [[NSAppleEventManager sharedAppleEventManager]
            setEventHandler:target
                andSelector:@selector(handleOpenDocuments:withReplyEvent:)
              forEventClass:kCoreEventClass
                 andEventID:kAEOpenDocuments];
    }
}

void DrainPendingOpenPaths(std::vector<std::string>& files,
                           std::vector<std::string>& directories) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_pending_paths.empty()) {
        files.insert(files.end(),
            std::make_move_iterator(g_pending_paths.begin()),
            std::make_move_iterator(g_pending_paths.end()));
        g_pending_paths.clear();
    }
    if (!g_pending_directories.empty()) {
        directories.insert(directories.end(),
            std::make_move_iterator(g_pending_directories.begin()),
            std::make_move_iterator(g_pending_directories.end()));
        g_pending_directories.clear();
    }
}

}

#endif
