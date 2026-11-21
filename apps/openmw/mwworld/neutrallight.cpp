#include "neutrallight.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <components/mwanimation/lightflicker.hpp>

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

    float neutralLightGain(const LightComponent& light)
    {
        if (!std::isfinite(light.fade) || light.fade <= 1.0 || light.negative)
            return 1.f;
        const int brightest = std::max({ int(light.red), int(light.green), int(light.blue) });
        if (brightest == 0)
            return 1.f;
        const double applied = std::min(light.fade, 255.0 / static_cast<double>(brightest));
        if (applied <= 0.0)
            return 1.f;
        const double gain = light.fade / applied;
        // A brightness past what any light of the masters has (fade 10) is not believed.
        return gain > 1.0 ? static_cast<float>(std::min(gain, 64.0)) : 1.f;
    }

    float neutralLightMovement(const LightComponent& light)
    {
        if (!std::isfinite(light.flickerMovement) || !std::isfinite(light.radius) || light.flickerMovement <= 0.0
            || light.radius <= 0.0)
            return 0.f;
        const double limit = std::min(static_cast<double>(MWAnim::NeutralFlicker::kMaxMovement),
            kMovementRadiusShare * light.radius);
        return static_cast<float>(std::min(light.flickerMovement, limit));
    }

    ESM::Light makeNeutralEsmLight(const LightComponent& light, bool fadeGain, bool ownFlicker, std::uint32_t seed,
        bool lightTypes, bool movement)
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
        if (fadeGain)
        {
            const float gain = neutralLightGain(light);
            if (gain > 1.f)
            {
                flags |= ESM::Light::NeutralGain;
                record.mData.mWeight = gain;
            }
        }
        if (ownFlicker && light.animation != LightAnimation::None)
        {
            // Task 0355 stage h: the light's own flicker or pulse numbers (a record with none usable keeps the rule).
            const auto narrow = [](double v) { return std::isfinite(v) ? static_cast<float>(v) : 0.f; };
            const std::int32_t packed = MWAnim::NeutralFlicker::packTimeField(
                narrow(light.flickerPeriod), narrow(light.flickerIntensity));
            if (packed != 0)
            {
                flags |= ESM::Light::NeutralFlicker;
                record.mData.mTime = packed;
                record.mData.mValue = static_cast<std::int32_t>(seed);
                if (movement)
                    record.mNeutralMovement = neutralLightMovement(light);
            }
        }
        if (lightTypes)
        {
            if (std::isfinite(light.spotFov) && light.spotFov >= 1.0 && light.spotFov <= 170.0)
            {
                flags |= ESM::Light::NeutralSpot;
                record.mNeutralSpotFov = static_cast<float>(light.spotFov);
            }
            if (light.shadow == openeden::sdk::game::LightShadow::Casts)
                flags |= ESM::Light::NeutralShadowCasts;
            else if (light.shadow == openeden::sdk::game::LightShadow::Never)
                flags |= ESM::Light::NeutralShadowNever;
        }
        record.mData.mFlags = flags;
        return record;
    }

    std::uint32_t neutralLightSeed(std::uint32_t contentFile, std::uint32_t index)
    {
        // splitmix64 of the placement's identity; only its low half is kept.
        std::uint64_t z = ((static_cast<std::uint64_t>(contentFile) << 32) | index) + 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return static_cast<std::uint32_t>((z ^ (z >> 31)) >> 16);
    }

    std::optional<NeutralGiLight> neutralGiLight(const LightComponent& light, bool fadeGain)
    {
        if (light.offByDefault)
            return std::nullopt;
        // The engine's own record: what the bake lights with is what scene.cpp hands to MWAnim::createObject.
        const ESM::Light record = makeNeutralEsmLight(light, fadeGain);
        if (record.mData.mRadius <= 0)
            return std::nullopt;
        NeutralGiLight out;
        out.mColor = static_cast<std::uint32_t>(record.mData.mColor);
        if ((record.mData.mFlags & ESM::Light::NeutralGain) != 0 && std::isfinite(record.mData.mWeight)
            && record.mData.mWeight > 0.f)
            out.mGain = record.mData.mWeight;
        out.mRadius = static_cast<float>(record.mData.mRadius);
        out.mNegative = (record.mData.mFlags & ESM::Light::Negative) != 0;
        return out;
    }
}
