// Both return refs to static locals so callers can take a program-lifetime .c_str().

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

            // In a .app, resourcePath ends in /Contents/Resources/. A loose binary (debugger, CI)
            // gets exe-dir, which has no assets — fall back to the source-tree path baked at configure.
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
            // Unsandboxed: NSHomeDirectory gives the real $HOME. Under a sandbox, switch to
            // NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).
            NSString* path = [NSString stringWithFormat:
                @"%@/Library/Application Support/hxediter",
                NSHomeDirectory()];

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

}

#endif
