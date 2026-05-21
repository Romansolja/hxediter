/* Apple impls of platform::ResourceDir() and platform::AppSupportDir().
 *
 * Both functions return references to static locals so callers can take
 * a stable .c_str() (in particular, io.IniFilename in main.cpp expects a
 * const char* that lives for the program lifetime). NSBundle / NSFileManager
 * are thread-safe for these read-only / idempotent-create patterns. */

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
            /* Unsandboxed flow — NSHomeDirectory() gives the user's real
             * $HOME. If this binary is ever sandboxed (e.g. for the Mac
             * App Store) switch to
             *   NSSearchPathForDirectoriesInDomains(
             *       NSApplicationSupportDirectory, NSUserDomainMask, YES)
             * which resolves correctly under both runtimes. */
            NSString* path = [NSString stringWithFormat:
                @"%@/Library/Application Support/hxediter",
                NSHomeDirectory()];

            /* If creation fails (corrupted home, sandbox spec mismatch,
             * disk full at the wrong moment) ImGui's later write of
             * imgui.ini lands in the void and the user gets a layout
             * that doesn't persist — same failure mode as the v1
             * "imgui.ini in cwd" bug we just fixed. Surface the
             * NSError to stderr so a future "why doesn't my dock
             * layout persist?" report has a breadcrumb. Non-fatal:
             * worst case we degrade to the pre-fix behavior. */
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

} /* namespace platform */

#endif /* __APPLE__ */
