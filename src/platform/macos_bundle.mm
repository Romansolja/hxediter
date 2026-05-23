// Apple impls of platform::ResourceDir() and platform::AppSupportDir().
// Both return references to static locals so callers can take a stable
// .c_str() (io.IniFilename in main.cpp expects a program-lifetime const
// char*). NSBundle / NSFileManager are thread-safe for these patterns.

#ifdef __APPLE__

#include "platform/asset_path.h"

#include <cstdio>

#import <Foundation/Foundation.h>

namespace platform {

const std::string& ResourceDir() {
    static const std::string cached = [] {
        @autoreleasepool {
            NSString* path = [[NSBundle mainBundle] resourcePath];
            if (path == nil) return std::string();
            return std::string([path UTF8String]) + "/";
        }
    }();
    return cached;
}

const std::string& AppSupportDir() {
    static const std::string cached = [] {
        @autoreleasepool {
            // Unsandboxed flow — NSHomeDirectory() gives the user's real
            // $HOME. If this binary is ever sandboxed (e.g. for the Mac
            // App Store) switch to NSSearchPathForDirectoriesInDomains(
            // NSApplicationSupportDirectory, NSUserDomainMask, YES), which
            // resolves correctly under both runtimes.
            NSString* path = [NSString stringWithFormat:
                @"%@/Library/Application Support/hxediter",
                NSHomeDirectory()];

            // If creation fails, ImGui's later imgui.ini write lands in
            // the void and the user's layout doesn't persist — same
            // failure mode as the "imgui.ini in cwd" bug. Surface NSError
            // to stderr so a future "why doesn't my layout persist?"
            // report has a breadcrumb. Non-fatal.
            NSError* err = nil;
            BOOL ok = [[NSFileManager defaultManager]
                createDirectoryAtPath:path
              withIntermediateDirectories:YES
                           attributes:nil
                                error:&err];
            if (!ok && err != nil) {
                fprintf(stderr,
                    "[platform::AppSupportDir] createDirectoryAtPath "
                    "failed for '%s': %s\n",
                    [path UTF8String],
                    [[err localizedDescription] UTF8String]);
            }
            return std::string([path UTF8String]) + "/";
        }
    }();
    return cached;
}

} // namespace platform

#endif // __APPLE__
