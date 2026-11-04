#include "viewlimits.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <system_error>

namespace Lod
{
    namespace
    {
        constexpr bool isSpace(char c) noexcept
        {
            return c == ' ' || c == '\t';
        }

        std::string_view trim(std::string_view text) noexcept
        {
            while (!text.empty() && isSpace(text.front()))
            {
                text.remove_prefix(1);
            }
            while (!text.empty() && isSpace(text.back()))
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
    }

    std::optional<ViewDistanceSetting> parseViewDistanceSetting(std::string_view text) noexcept
    {
        text = trim(text);
        if (text.empty())
        {
            return std::nullopt;
        }
        ViewDistanceSetting setting;
        if (equalsIgnoringCase(text, "auto"))
        {
            setting.mAuto = true;
            return setting;
        }
        float value = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc() || result.ptr != text.data() + text.size())
        {
            return std::nullopt;
        }
        if (!std::isfinite(value) || !(value > 0.f))
        {
            return std::nullopt;
        }
        setting.mValue = value;
        return setting;
    }

    LandExtent landExtent(std::span<const std::pair<std::int32_t, std::int32_t>> cells) noexcept
    {
        LandExtent extent;
        bool first = true;
        for (const auto& [x, y] : cells)
        {
            if (first)
            {
                extent.mMinX = extent.mMaxX = x;
                extent.mMinY = extent.mMaxY = y;
                first = false;
                continue;
            }
            extent.mMinX = std::min(extent.mMinX, x);
            extent.mMaxX = std::max(extent.mMaxX, x);
            extent.mMinY = std::min(extent.mMinY, y);
            extent.mMaxY = std::max(extent.mMaxY, y);
        }
        return extent;
    }

    double landDiagonal(const LandExtent& extent, double cellSize) noexcept
    {
        if (extent.empty())
        {
            return 0.0;
        }
        const double width = (static_cast<double>(extent.mMaxX) - static_cast<double>(extent.mMinX) + 1.0) * cellSize;
        const double height = (static_cast<double>(extent.mMaxY) - static_cast<double>(extent.mMinY) + 1.0) * cellSize;
        return std::hypot(width, height);
    }

    ViewLimits computeViewLimits(const ViewLimitsInput& input) noexcept
    {
        ViewLimits limits;
        if (!input.mLodActive)
        {
            limits.mTileRange = limits.mPagingRange = limits.mFarPlane = limits.mFogRange
                = input.mCameraViewingDistance;
            return limits;
        }

        const float number = input.mSetting.mAuto ? legacyViewDistance : input.mSetting.mValue;
        const float legacy = std::max(input.mCameraViewingDistance, number);
        if (!input.mMapView)
        {
            limits.mTileRange = limits.mPagingRange = limits.mFarPlane = limits.mFogRange = legacy;
            return limits;
        }

        float reach = number;
        if (input.mSetting.mAuto && input.mLandDiagonal > 0.0)
        {
            reach = static_cast<float>(std::min(input.mLandDiagonal, static_cast<double>(input.mMaxViewDistance)));
        }
        limits.mPagingRange = legacy;
        limits.mTileRange = std::max(legacy, reach);
        limits.mFarPlane = limits.mTileRange;
        limits.mFogRange = limits.mTileRange;
        return limits;
    }
}
