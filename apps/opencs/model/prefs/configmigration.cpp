#include "configmigration.hpp"

#include <system_error>

bool CSMPrefs::migrateLegacyConfig(const std::filesystem::path& userConfigDir) noexcept
{
    // The error_code overloads of std::filesystem report problems as values instead of throwing.
    std::error_code error;
    const std::filesystem::path current = userConfigDir / configFileName;
    const std::filesystem::path legacy = userConfigDir / legacyConfigFileName;

    if (std::filesystem::exists(current, error) || error)
        return false;
    if (!std::filesystem::is_regular_file(legacy, error) || error)
        return false;

    // copy_file with the default option fails if the target exists, so it can never overwrite.
    return std::filesystem::copy_file(legacy, current, std::filesystem::copy_options::none, error) && !error;
}
