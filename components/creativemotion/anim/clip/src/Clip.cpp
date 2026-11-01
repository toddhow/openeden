#include "opas/anim/Clip.hpp"

#include <cmath>
#include <set>
#include <string>

namespace opas::anim
{
    namespace
    {

        // Unit-length tolerance for rotation keys, as for rest rotations in rig files
        // (core/rig).
        constexpr double kUnitQuatTolerance = 1e-6;

        bool finite(math::Vec3 v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        bool finite(math::Quat q)
        {
            return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
        }

        // Key times strictly increasing, finite, within [0, duration], at least one.
        std::string checkTimes(
            const std::vector<double>& timesS, std::size_t valueCount, double durationS, const std::string& at)
        {
            if (timesS.empty())
            {
                return at + "/timesS: needs at least one key";
            }
            if (valueCount != timesS.size())
            {
                return at + "/values: needs one value per key time";
            }
            for (std::size_t i = 0; i < timesS.size(); ++i)
            {
                const std::string pointer = at + "/timesS/" + std::to_string(i);
                const double t = timesS[i];
                if (!std::isfinite(t) || t < 0.0 || t > durationS)
                {
                    return pointer + ": must be finite and within [0, durationS]";
                }
                if (i > 0 && !(t > timesS[i - 1]))
                {
                    return pointer + ": must be greater than the previous key time";
                }
            }
            return {};
        }

        std::string checkTrack(const Vec3Track& track, double durationS, const std::string& at)
        {
            if (const std::string problem = checkTimes(track.timesS, track.values.size(), durationS, at);
                !problem.empty())
            {
                return problem;
            }
            for (std::size_t i = 0; i < track.values.size(); ++i)
            {
                if (!finite(track.values[i]))
                {
                    return at + "/values/" + std::to_string(i) + ": must be finite";
                }
            }
            return {};
        }

        std::string checkTrack(const QuatTrack& track, double durationS, const std::string& at)
        {
            if (const std::string problem = checkTimes(track.timesS, track.values.size(), durationS, at);
                !problem.empty())
            {
                return problem;
            }
            for (std::size_t i = 0; i < track.values.size(); ++i)
            {
                const math::Quat q = track.values[i];
                const std::string pointer = at + "/values/" + std::to_string(i);
                if (!finite(q))
                {
                    return pointer + ": must be finite";
                }
                if (!(std::abs(math::length(q) - 1.0) <= kUnitQuatTolerance))
                {
                    return pointer + ": must be unit length";
                }
            }
            return {};
        }

    } // namespace

    std::string validateClip(const Clip& clip)
    {
        if (!(std::isfinite(clip.durationS) && clip.durationS > 0.0))
        {
            return "/durationS: must be positive and finite";
        }
        std::set<std::string> bones;
        for (std::size_t i = 0; i < clip.tracks.size(); ++i)
        {
            const std::string at = "/tracks/" + std::to_string(i);
            const BoneTrack& track = clip.tracks[i];
            if (track.bone.empty())
            {
                return at + "/bone: must not be empty";
            }
            if (!bones.insert(track.bone).second)
            {
                return at + "/bone: duplicate bone \"" + track.bone + "\"";
            }
            if (track.translation)
            {
                if (const std::string p = checkTrack(*track.translation, clip.durationS, at + "/translation");
                    !p.empty())
                {
                    return p;
                }
            }
            if (track.rotation)
            {
                if (const std::string p = checkTrack(*track.rotation, clip.durationS, at + "/rotation"); !p.empty())
                {
                    return p;
                }
            }
        }
        for (std::size_t i = 0; i < clip.events.size(); ++i)
        {
            const std::string at = "/events/" + std::to_string(i) + "/timeS";
            const double t = clip.events[i].timeS;
            if (!std::isfinite(t) || t < 0.0 || t > clip.durationS)
            {
                return at + ": must be finite and within [0, durationS]";
            }
            if (i > 0 && t < clip.events[i - 1].timeS)
            {
                return at + ": events must be sorted by time";
            }
        }
        if (!clip.rootBone.empty() && !bones.contains(clip.rootBone))
        {
            return "/rootBone: must name a track";
        }
        if (!clip.sourceRest.bones.empty())
        {
            if (const std::string problem = rig::validateSkeleton(clip.sourceRest); !problem.empty())
            {
                return "/sourceRest: " + problem;
            }
        }
        return {};
    }

} // namespace opas::anim
