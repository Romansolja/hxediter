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
            NSString* ns_path = [[NSBundle mainBundle] resourcePath];
            std::string path = (ns_path != nil)
                ? std::string([ns_path UTF8String]) + "/"
                : std::string();

            // Inside a .app bundle, resourcePath ends in /Contents/Resources/.
            // A loose executable (debugger, `build/hxediter` directly, CI
            // smoke test) gets the directory of the executable instead —
            // which doesn't contain assets/ — so the bundle would silently
            // ship the ImGui default font and look broken to a developer.
            // Detect that case, log it, and fall back to the source-tree
            // assets directory baked at configure time.
            const std::string suffix = "/Contents/Resources/";
            const bool in_bundle =
                path.size() >= suffix.size() &&
                path.compare(path.size() - suffix.size(),
                             suffix.size(), suffix) == 0;
            if (!in_bundle) {
#ifdef HXEDITER_SOURCE_DIR
                std::string fallback = std::string(HXEDITER_SOURCE_DIR) + "/";
                fprintf(stderr,
                    "[platform::ResourceDir] not running from a .app bundle "
                    "(resourcePath='%s'); using source-tree assets at '%s'\n",
                    path.c_str(), fallback.c_str());
                path = fallback;
#else
                fprintf(stderr,
                    "[platform::ResourceDir] not running from a .app bundle "
                    "(resourcePath='%s'); assets likely will not load\n",
                    path.c_str());
#endif
            }
            return path;
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

            // Return empty on creation failure so callers fall through to
            // their no-prefs behavior instead of writing into a path that
            // doesn't exist (which would log a second cryptic error on
            // every save). preferences.cpp:19 and main.cpp:224 both gate
            // on empty already — this finally makes those checks load-
            // bearing.
            NSError* err = nil;
            BOOL ok = [[NSFileManager defaultManager]
                createDirectoryAtPath:path
              withIntermediateDirectories:YES
                           attributes:nil
                                error:&err];
            if (!ok) {
                fprintf(stderr,
                    "[platform::AppSupportDir] createDirectoryAtPath "
                    "failed for '%s': %s\n",
                    [path UTF8String],
                    err ? [[err localizedDescription] UTF8String]
                        : "(no NSError reported)");
                return std::string();
            }
            return std::string([path UTF8String]) + "/";
        }
    }();
    return cached;
}

} // namespace platform

#endif // __APPLE__
