#ifndef CSM_PREFS_CONFIGMIGRATION_H
#define CSM_PREFS_CONFIGMIGRATION_H

#include <filesystem>

namespace CSMPrefs
{
    /// The editor's user settings file (task 0024, stage A: Eden Studio's name).
    inline constexpr const char* configFileName = "eden-studio.cfg";

    /// The file OpenMW-CS used. It is read once as a starting point and never written.
    inline constexpr const char* legacyConfigFileName = "openmw-cs.cfg";

    /// If \a userConfigDir has no eden-studio.cfg but has an openmw-cs.cfg, copies the old file to the new name.
    /// The old file is only read. Returns true when a copy was made. Does not throw: a failed copy means the
    /// editor starts with its defaults, which is what a first run does anyway.
    bool migrateLegacyConfig(const std::filesystem::path& userConfigDir) noexcept;
}

#endif
