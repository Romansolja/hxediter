#pragma once

#include <string>
#include <vector>

namespace platform {

// Install the kAEOpenDocuments handler. Must run before glfwInit(): glfwInit()
// calls [NSApp finishLaunching], which is when macOS dispatches any AppleEvents
// queued during launch — installing later loses the cold-launch "Open With" path.
void RegisterAppleEventHandlers();

// Move any paths/directories accumulated by the handler since the last call into
// the caller's vectors. Call once per main-loop iteration after glfwWaitEventsTimeout.
void DrainPendingOpenPaths(std::vector<std::string>& files,
                           std::vector<std::string>& directories);

}
