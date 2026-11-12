#include "animclip.hpp"

#include <components/animation/animset.hpp>
#include <components/animation/controllermap.hpp>
#include <components/animation/interpolate.hpp>
#include <components/misc/gameconventions.hpp>
#include <components/misc/strings/lower.hpp>

#include <vsg/maths/quat.h>
#include <vsg/maths/vec3.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <type_traits>
#include <utility>
#include <vector>

namespace MWRender
{
    namespace
    {
        // Clip length for a source with no keyed channel and no text key (as the OSG driver).
        constexpr float sMinimumClipDuration = 0.1f;

        opas::math::Vec3 toMetres(const vsg::vec3& v)
        {
            // The length unit is the game's convention (task 0085, move 1).
            const double invScale = 1.0 / Misc::gameConventions().mUnitsPerMeter;
            return { v.x * invScale, v.y * invScale, v.z * invScale };
        }

        opas::math::Quat toOpas(const vsg::quat& q)
        {
            opas::math::Quat result{ q.x, q.y, q.z, q.w };
            if (result.x == 0.0 && result.y == 0.0 && result.z == 0.0 && result.w == 0.0)
                return { 0.0, 0.0, 0.0, 1.0 };
            return opas::math::normalized(result);
        }

        /// Key times of the keyed channel types, or nullopt for any other channel.
        template <class T>
        std::optional<std::vector<float>> keyTimes(const Anim::Channel<T>& channel)
        {
            std::vector<float> times;
            const auto collect = [&](const auto& keys) {
                for (const auto& [time, value] : keys)
                    times.push_back(time);
            };
            if (const auto* linear = dynamic_cast<const Anim::Linear<T>*>(&channel))
                collect(linear->keys);
            else if (const auto* flip = dynamic_cast<const Anim::Flip<T>*>(&channel))
                collect(flip->keys);
            else if (const auto* spline = dynamic_cast<const Anim::CubicHermiteSpline<T>*>(&channel))
                collect(spline->keys);
            else
                return std::nullopt;
            return times;
        }

        /**
         * Converts one channel to a track (the rules of task 0007, R1, on vsgopenmw's channels):
         * - Linear keys, and CubicHermiteSpline<quat> keys (vsgopenmw interpolates those with vsg::mix, a slerp),
         * become linear keys at the same times: exact.
         * - Flip (constant) keys become step keys switching just after each midpoint, as Flip does.
         * - Anything else is resampled at sClipResampleRate over [0, duration], plus the key times of keyed channels.
         */
        template <class T, class Track, class Convert>
        Track convertChannel(const Anim::Channel<T>& channel, float duration, Convert&& convert)
        {
            Track track;
            const auto addKey = [&](double time, const T& value) {
                track.timesS.push_back(time);
                track.values.push_back(convert(value));
            };

            const auto* linear = dynamic_cast<const Anim::Linear<T>*>(&channel);
            const auto* flip = dynamic_cast<const Anim::Flip<T>*>(&channel);
            if (linear != nullptr && !linear->keys.empty())
            {
                for (const auto& [time, value] : linear->keys)
                    addKey(time, value);
                return track;
            }
            if constexpr (std::is_same_v<T, vsg::quat>)
            {
                const auto* spline = dynamic_cast<const Anim::CubicHermiteSpline<T>*>(&channel);
                if (spline != nullptr && !spline->keys.empty())
                {
                    for (const auto& [time, value] : spline->keys)
                        addKey(time, value);
                    return track;
                }
            }
            if (flip != nullptr && !flip->keys.empty())
            {
                track.interpolation = opas::anim::Interpolation::Step;
                auto it = flip->keys.begin();
                addKey(it->first, it->second);
                for (auto prev = it++; it != flip->keys.end(); prev = it++)
                {
                    const float midpoint = prev->first + (it->first - prev->first) * 0.5f;
                    addKey(std::nextafter(midpoint, std::numeric_limits<float>::infinity()), it->second);
                }
                return track;
            }

            std::set<float> times;
            if (auto keys = keyTimes(channel))
                times.insert(keys->begin(), keys->end());
            const int samples = static_cast<int>(std::ceil(duration * sClipResampleRate));
            for (int i = 0; i <= samples; ++i)
                times.insert(std::min(duration, static_cast<float>(i) / sClipResampleRate));
            for (float time : times)
                if (time >= 0.f)
                    addKey(time, channel.value(time));
            return track;
        }

        template <class T>
        void extendDuration(const Anim::Channel<T>* channel, float& duration)
        {
            if (channel == nullptr)
                return;
            if (auto keys = keyTimes(*channel); keys && !keys->empty())
                duration = std::max(duration, keys->back());
        }
    }

    std::shared_ptr<const AnimClip> buildAnimClip(const Anim::ControllerMap& source)
    {
        // The clip covers the source's text keys and every key of its keyed channels.
        float duration = 0.f;
        if (source.tags)
            for (const auto& [time, text] : *source.tags)
                duration = std::max(duration, time);

        // Controllers in target-name order, so the clip's track order does not depend on the source's hash map.
        std::vector<std::pair<std::string, const Anim::TransformController*>> controllers;
        for (const auto& [target, controller] : source.map)
        {
            if (controller == nullptr || (!controller->translate && !controller->rotate))
                continue;
            controllers.emplace_back(Misc::StringUtils::lowerCase(target), controller.get());
            extendDuration(controller->translate.get(), duration);
            extendDuration(controller->rotate.get(), duration);
        }
        if (controllers.empty())
            return nullptr;
        std::sort(controllers.begin(), controllers.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
        if (duration <= 0.f)
            duration = sMinimumClipDuration;

        auto result = std::make_shared<AnimClip>();
        result->mClip.name = "source_clip";
        result->mClip.durationS = duration;
        for (const auto& [bone, controller] : controllers)
        {
            opas::anim::BoneTrack track;
            track.bone = bone;
            if (controller->translate)
                track.translation = convertChannel<vsg::vec3, opas::anim::Vec3Track>(
                    *controller->translate, duration, [](const vsg::vec3& v) { return toMetres(v); });
            if (controller->rotate)
                track.rotation = convertChannel<vsg::quat, opas::anim::QuatTrack>(
                    *controller->rotate, duration, [](const vsg::quat& q) { return toOpas(q); });
            result->mClip.tracks.push_back(std::move(track));
        }
        return result;
    }

    std::shared_ptr<const AnimClip> animClipOf(const Anim::AnimSet& set)
    {
        return set.clip<AnimClip>([&set]() -> std::shared_ptr<const AnimClip> {
            return set.controllers != nullptr ? buildAnimClip(*set.controllers) : nullptr;
        });
    }
}
