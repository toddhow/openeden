#include "neutrallight.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace MWWorld
{
    using openeden::sdk::game::LightAnimation;
    using openeden::sdk::game::LightComponent;

    std::array<std::uint8_t, 3> neutralLightColor(const LightComponent& light)
    {
        const std::array<std::uint8_t, 3> base{ light.red, light.green, light.blue };
        if (!std::isfinite(light.fade) || light.fade <= 0.0)
            return light.fade == 0.0 ? std::array<std::uint8_t, 3>{ 0, 0, 0 } : base;
        const int brightest = std::max({ int(light.red), int(light.green), int(light.blue) });
        if (brightest == 0)
            return base;
        // The most the colour can be multiplied by without a channel passing 255.
        const double scale = std::min(light.fade, 255.0 / static_cast<double>(brightest));
        std::array<std::uint8_t, 3> out{};
        for (std::size_t i = 0; i < 3; ++i)
            out[i] = static_cast<std::uint8_t>(std::clamp(std::lround(static_cast<double>(base[i]) * scale), 0L, 255L));
        return out;
    }

    std::uint32_t packNeutralColor(const std::array<std::uint8_t, 3>& rgb)
    {
        return static_cast<std::uint32_t>(rgb[0]) | (static_cast<std::uint32_t>(rgb[1]) << 8)
            | (static_cast<std::uint32_t>(rgb[2]) << 16);
    }

    ESM::Light makeNeutralEsmLight(const LightComponent& light)
    {
        ESM::Light record;
        record.blank();
        const double radius = std::isfinite(light.radius) ? std::max(0.0, std::round(light.radius)) : 0.0;
        record.mData.mRadius = static_cast<std::int32_t>(
            std::min(radius, static_cast<double>(std::numeric_limits<std::int32_t>::max())));
        record.mData.mColor = packNeutralColor(neutralLightColor(light));
        std::int32_t flags = ESM::Light::Dynamic;
        switch (light.animation)
        {
            case LightAnimation::None:
                break;
            case LightAnimation::Flicker:
                flags |= ESM::Light::Flicker;
                break;
            case LightAnimation::FlickerSlow:
                flags |= ESM::Light::FlickerSlow;
                break;
            case LightAnimation::Pulse:
                flags |= ESM::Light::Pulse;
                break;
            case LightAnimation::PulseSlow:
                flags |= ESM::Light::PulseSlow;
                break;
        }
        if (light.negative)
            flags |= ESM::Light::Negative;
        record.mData.mFlags = flags;
        return record;
    }
}
