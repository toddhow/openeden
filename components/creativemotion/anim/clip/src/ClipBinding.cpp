#include "opas/anim/ClipBinding.hpp"

#include "opas/anim/Interpolate.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <utility>

namespace opas::anim
{
    namespace
    {

        math::Vec3 lerp(math::Vec3 a, math::Vec3 b, double t)
        {
            return a + (b - a) * t;
        }

        // Index i of the key interval [times[i], times[i+1]) holding t, and the
        // fraction along it; t outside the keys holds the nearest end.
        struct KeyPosition
        {
            std::size_t index = 0;
            double fraction = 0.0;
            bool atEnd = true; // only values[index] is used
        };

        KeyPosition locate(const std::vector<double>& times, double t)
        {
            if (!(t > times.front()))
            { // also catches NaN
                return { 0, 0.0, true };
            }
            if (t >= times.back())
            {
                return { times.size() - 1, 0.0, true };
            }
            const auto next = std::upper_bound(times.begin(), times.end(), t);
            const auto i = static_cast<std::size_t>(std::distance(times.begin(), next) - 1);
            return { i, (t - times[i]) / (times[i + 1] - times[i]), false };
        }

        math::Vec3 sampleTrack(const Vec3Track& track, double t)
        {
            const KeyPosition k = locate(track.timesS, t);
            if (k.atEnd || track.interpolation == Interpolation::Step)
            {
                return track.values[k.index];
            }
            return lerp(track.values[k.index], track.values[k.index + 1], k.fraction);
        }

        math::Quat sampleTrack(const QuatTrack& track, double t)
        {
            const KeyPosition k = locate(track.timesS, t);
            if (k.atEnd || track.interpolation == Interpolation::Step)
            {
                return math::normalized(track.values[k.index]);
            }
            return slerp(track.values[k.index], track.values[k.index + 1], k.fraction);
        }

        // Below this squared size of the twist part (z, w) the twist about +Z is
        // undefined (a half turn about a horizontal axis), and the yaw is taken as 0
        // so the rotation is left as it is; atan2 of rounding noise would give an
        // arbitrary angle.
        constexpr double kMinTwistSquared = 1e-12;

        // The twist about +Z of a rotation (swing-twist decomposition), in (-pi, pi].
        double yawOf(math::Quat q)
        {
            if (q.z * q.z + q.w * q.w < kMinTwistSquared)
            {
                return 0.0;
            }
            return 2.0 * std::atan2(q.z, q.w);
        }

        // An angle wrapped into (-pi, pi].
        double wrapAngle(double a)
        {
            constexpr double kTwoPi = 2.0 * std::numbers::pi;
            a = std::fmod(a + std::numbers::pi, kTwoPi);
            if (a <= 0.0)
            {
                a += kTwoPi;
            }
            return a - std::numbers::pi;
        }

        math::Vec3 rotateZ(math::Vec3 v, double angle)
        {
            const double c = std::cos(angle);
            const double s = std::sin(angle);
            return { c * v.x - s * v.y, s * v.x + c * v.y, 0.0 };
        }

        // A rest rotation counts as none within the unit-quaternion tolerance of rig
        // files (core/rig): |w| = 1 to that precision.
        constexpr double kIdentityTolerance = 1e-6;

        bool rotates(const math::Transform& rest)
        {
            return std::abs(std::abs(rest.rotation.w) - 1.0) > kIdentityTolerance;
        }

        // The sum of n planar moves t, the k-th rotated by k * yaw (k = 0 .. n - 1),
        // in closed form: a rotation by (n - 1) * yaw / 2 scaled by
        // sin(n yaw / 2) / sin(yaw / 2). Where sin(yaw / 2) vanishes the yaw is a
        // whole number of turns, every rotation is the identity, and the sum is n * t
        // (the formula's limit there carries a sign that only the rotation undoes).
        math::Vec3 repeatedMove(math::Vec3 t, double yawRad, double n)
        {
            constexpr double kTinySin = 1e-9;
            const double s = std::sin(0.5 * yawRad);
            if (std::abs(s) < kTinySin)
            {
                return t * n;
            }
            return rotateZ(t, 0.5 * (n - 1.0) * yawRad) * (std::sin(0.5 * n * yawRad) / s);
        }

        double chainLength(const std::vector<math::Transform>& worldRest, const std::vector<std::size_t>& bones)
        {
            double length = 0.0;
            for (std::size_t i = 1; i < bones.size(); ++i)
            {
                length += math::distance(worldRest[bones[i - 1]].translation, worldRest[bones[i]].translation);
            }
            return length;
        }

    } // namespace

    std::optional<ClipBinding> ClipBinding::create(
        const Clip& clip, const rig::Skeleton& target, const SkeletonProfile& profile, std::string* error)
    {
        const auto failWith = [error](std::string message) -> std::optional<ClipBinding> {
            if (error != nullptr)
            {
                *error = std::move(message);
            }
            return std::nullopt;
        };
        if (const std::string problem = validateClip(clip); !problem.empty())
        {
            return failWith("clip \"" + clip.name + "\": " + problem);
        }
        if (const std::string problem = validateSkeletonProfile(profile); !problem.empty())
        {
            return failWith("skeleton profile \"" + profile.name + "\": " + problem);
        }
        if (const std::string problem = rig::validateSkeleton(target); !problem.empty())
        {
            return failWith("target skeleton: " + problem);
        }

        ClipBinding b;
        b.clip_ = clip;
        // Profile: source bone name -> target bone index.
        std::map<std::string, std::size_t> targetOfSource;
        for (const BoneMapping& m : profile.bones)
        {
            const std::optional<std::size_t> index = rig::findBone(target, m.target);
            if (!index)
            {
                return failWith("skeleton profile \"" + profile.name + "\" maps to bone \"" + m.target
                    + "\", which the target skeleton lacks");
            }
            targetOfSource[m.source] = *index;
        }
        for (const rig::Bone& bone : target.bones)
        {
            b.targetRest_.push_back(bone.localRest);
        }
        b.trackOfTarget_.assign(target.bones.size(), -1);
        for (std::size_t i = 0; i < clip.tracks.size(); ++i)
        {
            const auto found = targetOfSource.find(clip.tracks[i].bone);
            if (found == targetOfSource.end())
            {
                b.unmappedClipBones_.push_back(clip.tracks[i].bone);
                continue;
            }
            b.trackOfTarget_[found->second] = static_cast<int>(i);
            if (!clip.rootBone.empty() && clip.tracks[i].bone == clip.rootBone)
            {
                b.rootTarget_ = static_cast<int>(found->second);
            }
        }
        for (std::size_t t = 0; t < target.bones.size(); ++t)
        {
            if (b.trackOfTarget_[t] < 0)
            {
                b.untrackedTargetBones_.push_back(t);
            }
        }

        // The root bone's track even when the profile does not map it: root motion
        // comes from the clip either way.
        std::map<std::string, int> trackOfBone;
        for (std::size_t i = 0; i < clip.tracks.size(); ++i)
        {
            trackOfBone[clip.tracks[i].bone] = static_cast<int>(i);
        }
        if (!clip.rootBone.empty())
        {
            b.rootTrack_ = trackOfBone.at(clip.rootBone);
            // Path from the clip skeleton's top down to the root bone; without a
            // source skeleton (or without the bone in it) the root is the top.
            const std::optional<std::size_t> inSource = rig::findBone(clip.sourceRest, clip.rootBone);
            if (inSource)
            {
                std::vector<ChainLink> upwards;
                for (int bone = static_cast<int>(*inSource); bone >= 0;
                     bone = clip.sourceRest.bones[static_cast<std::size_t>(bone)].parent)
                {
                    const rig::Bone& sb = clip.sourceRest.bones[static_cast<std::size_t>(bone)];
                    const auto track = trackOfBone.find(sb.name);
                    upwards.push_back(ChainLink{ track == trackOfBone.end() ? -1 : track->second, sb.localRest });
                }
                b.rootChain_.assign(upwards.rbegin(), upwards.rend());
                // Above the root nothing may rotate, so the root's twist is the
                // twist of its own local rotation and removing it in the local
                // frame is exact (C3a implementation notes; readers bake container
                // nodes into the root).
                for (std::size_t i = 0; i + 1 < b.rootChain_.size(); ++i)
                {
                    const ChainLink& link = b.rootChain_[i];
                    if (rotates(link.rest)
                        || (link.track >= 0 && clip.tracks[static_cast<std::size_t>(link.track)].rotation))
                    {
                        return failWith("clip \"" + clip.name + "\": /rootBone: an ancestor of the root bone rotates");
                    }
                }
            }
            else
            {
                b.rootChain_.push_back(ChainLink{ b.rootTrack_, math::Transform{} });
            }
            // A mapped root without a rotation channel keeps the target's rest
            // rotation in the pose, so root motion is measured from that same
            // rotation (C3a implementation notes).
            if (b.rootTarget_ >= 0 && !clip.tracks[static_cast<std::size_t>(b.rootTrack_)].rotation)
            {
                b.rootChain_.back().rest.rotation = b.targetRest_[static_cast<std::size_t>(b.rootTarget_)].rotation;
            }
            if (b.rootTarget_ >= 0)
            {
                const auto top = static_cast<std::size_t>(b.rootTarget_);
                for (int bone = target.bones[top].parent; bone >= 0;
                     bone = target.bones[static_cast<std::size_t>(bone)].parent)
                {
                    const auto at = static_cast<std::size_t>(bone);
                    const int track = b.trackOfTarget_[at];
                    if (rotates(target.bones[at].localRest)
                        || (track >= 0 && clip.tracks[static_cast<std::size_t>(track)].rotation))
                    {
                        return failWith("target skeleton: bone \"" + target.bones[at].name
                            + "\", an ancestor of the root bone, rotates");
                    }
                }
            }
            // Unwrapped yaw at every rotation key of the root, plus both ends:
            // only the root rotates in its chain, and between consecutive stored
            // times it turns by less than pi (shortest-path interpolation), so
            // wrapping the difference unwraps.
            std::vector<double> times{ 0.0, clip.durationS };
            if (const auto& rotation = clip.tracks[static_cast<std::size_t>(b.rootTrack_)].rotation)
            {
                times.insert(times.end(), rotation->timesS.begin(), rotation->timesS.end());
            }
            std::sort(times.begin(), times.end());
            times.erase(std::unique(times.begin(), times.end()), times.end());
            b.yawKeyTimesS_ = times;
            double previous = yawOf(b.rootModel(times.front()).rotation);
            double unwrapped = previous;
            for (const double t : times)
            {
                const double yaw = yawOf(b.rootModel(t).rotation);
                unwrapped += wrapAngle(yaw - previous);
                previous = yaw;
                b.yawKeyRad_.push_back(yaw);
                b.yawKeyUnwrappedRad_.push_back(unwrapped);
            }
        }

        // Leg-length scaling of the root's translation (docs/formats/skeletonprofile.md).
        if (!profile.legChain.empty() && !clip.sourceRest.bones.empty())
        {
            std::map<std::string, std::string> sourceOfTarget;
            for (const BoneMapping& m : profile.bones)
            {
                sourceOfTarget[m.target] = m.source;
            }
            std::vector<std::size_t> targetChain;
            std::vector<std::size_t> sourceChain;
            for (const std::string& name : profile.legChain)
            {
                const std::optional<std::size_t> t = rig::findBone(target, name);
                const std::optional<std::size_t> s = rig::findBone(clip.sourceRest, sourceOfTarget.at(name));
                if (!t || !s)
                {
                    targetChain.clear();
                    break;
                }
                targetChain.push_back(*t);
                sourceChain.push_back(*s);
            }
            if (!targetChain.empty())
            {
                const double targetLength = chainLength(rig::computeWorldRest(target), targetChain);
                const double sourceLength = chainLength(rig::computeWorldRest(clip.sourceRest), sourceChain);
                if (sourceLength > 0.0 && targetLength > 0.0)
                {
                    b.rootScale_ = targetLength / sourceLength;
                }
            }
        }
        return b;
    }

    std::span<const std::string> ClipBinding::unmappedClipBones() const
    {
        return unmappedClipBones_;
    }

    std::span<const std::size_t> ClipBinding::untrackedTargetBones() const
    {
        return untrackedTargetBones_;
    }

    double ClipBinding::rootTranslationScale() const
    {
        return rootScale_;
    }

    math::Transform ClipBinding::rootModel(double timeS) const
    {
        math::Transform model;
        for (const ChainLink& link : rootChain_)
        {
            math::Transform local = link.rest;
            if (link.track >= 0)
            {
                const BoneTrack& track = clip_.tracks[static_cast<std::size_t>(link.track)];
                if (track.translation)
                {
                    local.translation = sampleTrack(*track.translation, timeS);
                }
                if (track.rotation)
                {
                    local.rotation = sampleTrack(*track.rotation, timeS);
                }
            }
            model = model * local;
        }
        return model;
    }

    double ClipBinding::unwrappedYaw(double timeS) const
    {
        const KeyPosition k = locate(yawKeyTimesS_, timeS);
        const double atKey = yawKeyUnwrappedRad_[k.index];
        return atKey + wrapAngle(yawOf(rootModel(timeS).rotation) - yawKeyRad_[k.index]);
    }

    RootMotionDelta ClipBinding::segment(double aS, double bS) const
    {
        const math::Vec3 pa = rootModel(aS).translation;
        const math::Vec3 pb = rootModel(bS).translation;
        const double yawA = unwrappedYaw(aS);
        const math::Vec3 horizontal{ (pb.x - pa.x) * rootScale_, (pb.y - pa.y) * rootScale_, 0.0 };
        return RootMotionDelta{ rotateZ(horizontal, -yawA), unwrappedYaw(bS) - yawA };
    }

    RootMotionDelta ClipBinding::rootMotion(double fromS, double toS, TimeMode mode) const
    {
        if (rootTrack_ < 0 || !std::isfinite(fromS) || !std::isfinite(toS) || !(toS > fromS))
        {
            return {};
        }
        const double duration = clip_.durationS;
        if (mode == TimeMode::Clamp)
        {
            return segment(std::clamp(fromS, 0.0, duration), std::clamp(toS, 0.0, duration));
        }
        // Loop: compose the partial first cycle, whole cycles, and the partial
        // last cycle as successive planar moves (each in the frame where the
        // previous one ended).
        const double cycleFrom = std::floor(fromS / duration);
        const double cycleTo = std::floor(toS / duration);
        const double a = fromS - cycleFrom * duration;
        const double b = toS - cycleTo * duration;
        RootMotionDelta total;
        const auto append = [&total](const RootMotionDelta& d) {
            total.translationM = total.translationM + rotateZ(d.translationM, total.yawRad);
            total.yawRad += d.yawRad;
        };
        if (cycleFrom == cycleTo)
        {
            return segment(a, b);
        }
        append(segment(a, duration));
        // n whole cycles in closed form, so the cost does not grow with n.
        const RootMotionDelta cycle = segment(0.0, duration);
        const double n = cycleTo - cycleFrom - 1.0;
        if (n > 0.0)
        {
            append(RootMotionDelta{ repeatedMove(cycle.translationM, cycle.yawRad, n), n * cycle.yawRad });
        }
        append(segment(0.0, b));
        return total;
    }

    void ClipBinding::sample(double timeS, TimeMode mode, std::span<math::Transform> localPose) const
    {
        if (localPose.size() < targetRest_.size())
        {
            return;
        }
        const double duration = clip_.durationS;
        double t = 0.0;
        if (!std::isfinite(timeS))
        {
            t = 0.0; // a non-finite time samples the start (header)
        }
        else if (mode == TimeMode::Loop)
        {
            t = timeS - std::floor(timeS / duration) * duration;
            if (t >= duration)
            { // rounding at an exact multiple
                t = 0.0;
            }
        }
        else
        {
            t = std::clamp(timeS, 0.0, duration);
        }
        for (std::size_t i = 0; i < targetRest_.size(); ++i)
        {
            math::Transform pose = targetRest_[i];
            const int trackIndex = trackOfTarget_[i];
            if (trackIndex >= 0)
            {
                const BoneTrack& track = clip_.tracks[static_cast<std::size_t>(trackIndex)];
                if (track.rotation)
                {
                    pose.rotation = sampleTrack(*track.rotation, t);
                }
                if (static_cast<int>(i) == rootTarget_ && track.translation)
                {
                    pose.translation = sampleTrack(*track.translation, t) * rootScale_;
                }
            }
            localPose[i] = pose;
        }
        if (rootTarget_ >= 0)
        {
            // Root motion moves the character, so the root keeps its rest x and y
            // and loses its yaw (task 0020, C3a interface).
            math::Transform& root = localPose[static_cast<std::size_t>(rootTarget_)];
            const math::Transform& rest = targetRest_[static_cast<std::size_t>(rootTarget_)];
            root.translation.x = rest.translation.x;
            root.translation.y = rest.translation.y;
            // Nothing above the root rotates (create), so the root's model twist
            // is the twist of the local rotation the pose uses.
            const double yaw = yawOf(root.rotation);
            root.rotation = math::normalized(math::fromAxisAngle(math::Vec3{ 0.0, 0.0, 1.0 }, -yaw) * root.rotation);
        }
    }

} // namespace opas::anim
