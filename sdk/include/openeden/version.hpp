#pragma once

#include <cstdint>
#include <ostream>

namespace openeden::sdk
{
    /**
     * @brief Version represented as a major and minor number.
     *
     * Compatibility rule (Section 4.1/4.2):
     * A minor version only adds features: a consumer built for 1.0 works with a 1.3 provider.
     * A major version changes the contract: major versions must match exactly.
     */
    struct Version
    {
        std::uint16_t major = 0;
        std::uint16_t minor = 0;
    };

    constexpr bool operator==(const Version& lhs, const Version& rhs) noexcept
    {
        return lhs.major == rhs.major && lhs.minor == rhs.minor;
    }

    constexpr bool operator!=(const Version& lhs, const Version& rhs) noexcept
    {
        return !(lhs == rhs);
    }

    constexpr bool operator<(const Version& lhs, const Version& rhs) noexcept
    {
        if (lhs.major != rhs.major)
            return lhs.major < rhs.major;
        return lhs.minor < rhs.minor;
    }

    constexpr bool operator<=(const Version& lhs, const Version& rhs) noexcept
    {
        return !(rhs < lhs);
    }

    constexpr bool operator>(const Version& lhs, const Version& rhs) noexcept
    {
        return rhs < lhs;
    }

    constexpr bool operator>=(const Version& lhs, const Version& rhs) noexcept
    {
        return !(lhs < rhs);
    }

    /**
     * @brief Checks if a provided version satisfies a required version.
     *
     * Returns true if provided.major == required.major and provided.minor >= required.minor.
     */
    constexpr bool isCompatible(const Version& required, const Version& provided) noexcept
    {
        return provided.major == required.major && provided.minor >= required.minor;
    }

    /**
     * @brief Current version of the OpenEden SDK.
     */
    constexpr Version kSdkVersion{ 4, 0 };
    constexpr Version kVersion = kSdkVersion;

    inline std::ostream& operator<<(std::ostream& os, const Version& v)
    {
        return os << v.major << '.' << v.minor;
    }
} // namespace openeden::sdk
