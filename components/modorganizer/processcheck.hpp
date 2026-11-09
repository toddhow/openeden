#ifndef OPENEDEN_COMPONENTS_MODORGANIZER_PROCESSCHECK_H
#define OPENEDEN_COMPONENTS_MODORGANIZER_PROCESSCHECK_H

// A best-effort "is the game running" check by executable name (task 0057 D14, task 0058): Windows walks the
// process snapshot, Linux reads /proc/<pid>/comm. Names compare ASCII case-insensitively, without a ".exe" suffix.
// False negatives are possible (a renamed executable); the callers say so.

#include <string>
#include <string_view>

namespace ModOrganizer
{
    bool isProcessRunning(std::string_view executableName);
    /// The name of the current process without directory and ".exe" (for tests).
    std::string currentProcessName();
}

#endif
