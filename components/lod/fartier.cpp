#include "fartier.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <string_view>
#include <utility>

#include "viewlimits.hpp"

namespace Lod
{
    namespace
    {
        constexpr std::uint64_t gibibyte = 1024ull * 1024 * 1024;
        constexpr std::uint64_t mebibyte = 1024ull * 1024;

        // The tier a card qualifies for by its device memory: starting values, see the stage log of task 0066.
        FarTier tierOfDeviceMemory(std::uint64_t bytes) noexcept
        {
            if (bytes < 2 * gibibyte)
            {
                return FarTier::Minimal;
            }
            if (bytes < 4 * gibibyte)
            {
                return FarTier::Low;
            }
            if (bytes < 8 * gibibyte)
            {
                return FarTier::Medium;
            }
            return FarTier::High;
        }

        // The tier the machine qualifies for by its system memory: the game, its caches and the far field share it.
        FarTier tierOfSystemMemory(std::uint64_t bytes) noexcept
        {
            if (bytes < 6 * gibibyte)
            {
                return FarTier::Minimal;
            }
            if (bytes < 12 * gibibyte)
            {
                return FarTier::Low;
            }
            if (bytes < 24 * gibibyte)
            {
                return FarTier::Medium;
            }
            return FarTier::High;
        }
    }

    FarTier chooseFarTier(const HardwareReport& report) noexcept
    {
        if (report.mSoftwareDevice || report.mDeviceMemoryBytes == 0 || report.mSystemMemoryBytes == 0)
        {
            return FarTier::Minimal;
        }
        return std::min(tierOfDeviceMemory(report.mDeviceMemoryBytes), tierOfSystemMemory(report.mSystemMemoryBytes));
    }

    FarTierSettings farTierSettings(FarTier tier) noexcept
    {
        FarTierSettings settings;
        switch (tier)
        {
            case FarTier::Minimal:
                settings.mFarLevels = 3; // today's reach (spec 0066, "Low-end hardware")
                settings.mTileThreshold = 16;
                settings.mMaxViewDistance = 80000;
                settings.mFarTextureMaxSize = 128;
                settings.mResidentBudgetBytes = 256 * mebibyte;
                // Tile budgets (task 0060 reviews): what the drawn tiles hold, read from their draws: about 52 KiB
                // a tile with the live layers, plus a 256 KiB ground picture from level 2 up without the terrain
                // maps (the default). Measured on the climb to 25,000 units with map view and view distance auto:
                // with the live layers at most 89 tiles and 4.5 MiB; with pictures (the default settings) at most
                // 61 tiles and 9.6 MiB (high tier). Every tier keeps that set whole. The budget governs only the tiles
                // inside the live range: those past it are drawn whatever it says (second review).
                settings.mTerrainBudgetBytes = 16 * mebibyte;
                settings.mEvictFarClusters = true; // task 0060, decision of 2026-11-04
                break;
            case FarTier::Low:
                settings.mFarLevels = 4;
                settings.mTileThreshold = 16;
                settings.mMaxViewDistance = 150000;
                settings.mFarTextureMaxSize = 128;
                settings.mResidentBudgetBytes = 320 * mebibyte;
                settings.mTerrainBudgetBytes = 24 * mebibyte;
                settings.mEvictFarClusters = true;
                break;
            case FarTier::Medium:
                settings.mFarLevels = 5;
                settings.mTileThreshold = 8;
                settings.mMaxViewDistance = 300000;
                settings.mFarTextureMaxSize = 256;
                settings.mResidentBudgetBytes = 512 * mebibyte;
                settings.mTerrainBudgetBytes = 32 * mebibyte;
                break;
            case FarTier::High:
                settings.mFarLevels = maxFarLevels;
                settings.mTileThreshold = 8;
                settings.mMaxViewDistance = defaultMaxViewDistance;
                settings.mFarTextureMaxSize = 256;
                settings.mResidentBudgetBytes = 1024 * mebibyte;
                settings.mTerrainBudgetBytes = 64 * mebibyte;
                break;
        }
        return settings;
    }

    std::uint32_t resolveFarLevels(std::int32_t setting, std::uint32_t tierLevels) noexcept
    {
        const std::uint32_t tier = std::clamp<std::uint32_t>(tierLevels, 1, maxFarLevels);
        if (setting <= 0)
        {
            return tier;
        }
        return std::min<std::uint32_t>(static_cast<std::uint32_t>(setting), maxFarLevels);
    }

    std::uint32_t farLevelsWithinBudget(
        std::span<const std::uint64_t> levelBytes, std::uint64_t budgetBytes, std::uint32_t wanted) noexcept
    {
        const std::uint32_t wantedLevels = std::clamp<std::uint32_t>(wanted, 1, maxFarLevels);
        if (levelBytes.empty())
        {
            return wantedLevels;
        }
        const std::size_t limit = std::min<std::size_t>(wantedLevels, levelBytes.size());
        if (budgetBytes == 0)
        {
            return static_cast<std::uint32_t>(limit);
        }
        // Level 1 is the base of the far field: it stays whatever it costs.
        std::uint64_t sum = levelBytes[0];
        std::uint32_t levels = 1;
        for (std::size_t i = 1; i < limit; ++i)
        {
            if (sum > std::numeric_limits<std::uint64_t>::max() - levelBytes[i])
            {
                break; // the sum would wrap around: it does not fit
            }
            const std::uint64_t next = sum + levelBytes[i];
            if (next > budgetBytes)
            {
                break;
            }
            sum = next;
            ++levels;
        }
        return levels;
    }
}

namespace Lod
{
    namespace
    {
        constexpr std::int32_t minFarTextureSize = 16;
        constexpr std::int32_t maxFarTextureSize = 8192;

        std::string_view trimSpaces(std::string_view text) noexcept
        {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
            {
                text.remove_prefix(1);
            }
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
            {
                text.remove_suffix(1);
            }
            return text;
        }

        bool equalsIgnoringCase(std::string_view text, std::string_view lowerCaseWord) noexcept
        {
            if (text.size() != lowerCaseWord.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                char c = text[i];
                if (c >= 'A' && c <= 'Z')
                {
                    c = static_cast<char>(c - 'A' + 'a');
                }
                if (c != lowerCaseWord[i])
                {
                    return false;
                }
            }
            return true;
        }

        // A size cut to 16 .. 8192 and rounded down to a power of two.
        std::uint32_t powerOfTwoSize(std::int64_t value) noexcept
        {
            const auto clamped
                = static_cast<std::uint32_t>(std::clamp<std::int64_t>(value, minFarTextureSize, maxFarTextureSize));
            return std::bit_floor(clamped);
        }
    }

    FarTierSetting parseFarTierSetting(std::string_view text) noexcept
    {
        FarTierSetting setting;
        const std::string_view trimmed = trimSpaces(text);
        if (trimmed.empty() || equalsIgnoringCase(trimmed, "auto"))
        {
            return setting;
        }
        const std::pair<std::string_view, FarTier> names[] = { { "minimal", FarTier::Minimal }, { "low", FarTier::Low },
            { "medium", FarTier::Medium }, { "high", FarTier::High } };
        for (const auto& [name, tier] : names)
        {
            if (equalsIgnoringCase(trimmed, name))
            {
                setting.mAuto = false;
                setting.mTier = tier;
                return setting;
            }
        }
        setting.mValid = false;
        return setting;
    }

    FarTier resolveFarTier(const FarTierSetting& setting, const HardwareReport& report) noexcept
    {
        return setting.mAuto ? chooseFarTier(report) : setting.mTier;
    }

    float effectiveTileThreshold(float setting, const FarTierSettings& tier) noexcept
    {
        return std::max(setting, tier.mTileThreshold);
    }

    float effectiveMaxViewDistance(float setting, const FarTierSettings& tier) noexcept
    {
        return setting > 0.f ? std::min(setting, tier.mMaxViewDistance) : tier.mMaxViewDistance;
    }

    std::uint32_t resolveFarTextureSize(std::int32_t setting, std::uint32_t tierSize) noexcept
    {
        if (setting > 0)
        {
            return powerOfTwoSize(setting);
        }
        return powerOfTwoSize(static_cast<std::int64_t>(tierSize));
    }
}

namespace Lod
{
    namespace
    {
        // a * b, or the largest value when the product does not fit.
        std::uint64_t saturatingMultiply(std::uint64_t a, std::uint64_t b) noexcept
        {
            if (a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a)
            {
                return std::numeric_limits<std::uint64_t>::max();
            }
            return a * b;
        }

        std::uint64_t saturatingAdd(std::uint64_t a, std::uint64_t b) noexcept
        {
            if (a > std::numeric_limits<std::uint64_t>::max() - b)
            {
                return std::numeric_limits<std::uint64_t>::max();
            }
            return a + b;
        }
    }

    std::uint64_t clusterResidentBytes(std::uint64_t vertexCount, std::uint64_t indexCount) noexcept
    {
        return saturatingAdd(saturatingMultiply(vertexCount, clusterBytesPerVertex),
            saturatingMultiply(indexCount, clusterBytesPerIndex));
    }

    bool admitCluster(std::uint64_t residentBytes, std::uint64_t addBytes, std::uint64_t budgetBytes) noexcept
    {
        if (budgetBytes == 0 || residentBytes == 0)
        {
            return true;
        }
        return addBytes <= budgetBytes && residentBytes <= budgetBytes - addBytes;
    }
}
