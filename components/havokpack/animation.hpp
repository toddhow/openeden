#ifndef OPENEDEN_COMPONENTS_HAVOKPACK_ANIMATION_H
#define OPENEDEN_COMPONENTS_HAVOKPACK_ANIMATION_H

// Task 0119: hkaSplineCompressedAnimation decoded from a Packfile, and sampled at a time. The layout was established
// from the player's own files and is logged in docs/research/0054-provenance.md (F17 to F34); a block whose track data
// does not end exactly where the file says is refused, so a layout we have not seen cannot decode silently wrong.
//
// Formats layer, standard library only. Ownership: a Clip owns everything it holds. A Clip is immutable once read,
// so sampling it from several threads is safe. sample() allocates nothing.

#include "packfile.hpp"
#include "skeleton.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace HavokPack
{
    struct ClipLimits
    {
        std::uint32_t mMaxTracks = 4096;
        std::uint32_t mMaxBlocks = 4096;
        std::uint32_t mMaxFramesPerBlock = 1024;
        std::uint32_t mMaxFrames = 1u << 20;
        std::uint32_t mMaxSplineItems = 1024;
    };

    // A uniform clamped B-spline over frame numbers inside a block: control points of `mDims` floats each.
    struct Spline
    {
        std::uint8_t mDegree = 0;
        std::vector<std::uint8_t> mKnots; // items + degree + 1 knots
        std::uint32_t mItems = 0; // number of control points
        std::uint32_t mDims = 0;
        std::vector<float> mControl; // mItems * mDims
    };

    // Position or scale: per axis constant, or (for the axes in mSplineAxes) animated by one spline.
    struct VectorTrack
    {
        std::array<float, 3> mConstant{ 0, 0, 0 };
        std::uint8_t mSplineAxes = 0; // bit k = axis k is animated
        Spline mSpline; // one dimension per animated axis, in axis order
    };

    struct RotationTrack
    {
        enum class Kind : std::uint8_t
        {
            Identity,
            Constant,
            Spline,
        };
        Kind mKind = Kind::Identity;
        std::array<float, 4> mConstant{ 0, 0, 0, 1 }; // x y z w
        Spline mSpline; // four dimensions; neighbouring points are in one hemisphere
    };

    // A float track in one block (stage 7): one value, or a spline of one dimension. Skyrim SE uses them for the
    // visibility and fade slots of the skeleton ("hkVis:Weapon", "hkFade:AnimObjectA", ...), provenance F37 to F43.
    struct FloatTrackBlock
    {
        bool mAnimated = false;
        float mConstant = 0;
        Spline mSpline; // one dimension, when mAnimated
    };

    struct TrackBlock
    {
        VectorTrack mPosition;
        RotationTrack mRotation;
        VectorTrack mScale;
    };

    struct Clip
    {
        float mDuration = 0;
        float mFrameDuration = 0;
        float mBlockDuration = 0;
        std::uint32_t mNumFrames = 0;
        std::uint32_t mMaxFramesPerBlock = 0;
        std::uint32_t mNumTransformTracks = 0;
        std::uint32_t mNumFloatTracks = 0;
        // Bone index of each transform track; empty means track i drives bone i.
        std::vector<std::int16_t> mTrackToBone;
        std::string mSkeletonName;
        // Floats of the file that were NaN or infinite and were replaced by a track's default (see provenance F36).
        std::uint32_t mNonFiniteValues = 0;
        // [block][track]
        std::vector<std::vector<TrackBlock>> mBlocks;
        // [block][float track]; empty when the clip has no float tracks
        std::vector<std::vector<FloatTrackBlock>> mFloatBlocks;
        // Float slot of each float track (the skeleton's mFloatSlots index); empty means track i drives slot i.
        std::vector<std::int16_t> mFloatTrackToSlot;

        // The bone each track drives.
        std::uint32_t boneOfTrack(std::uint32_t track) const
        {
            return mTrackToBone.empty() ? track : static_cast<std::uint32_t>(mTrackToBone[track]);
        }
        std::uint32_t slotOfFloatTrack(std::uint32_t track) const
        {
            return mFloatTrackToSlot.empty() ? track : static_cast<std::uint32_t>(mFloatTrackToSlot[track]);
        }
    };

    // Every hkaSplineCompressedAnimation of the file with its binding. Throws Error.
    std::vector<Clip> readClips(const Packfile& file, const ClipLimits& limits = {});

    // 40-bit quaternion (five bytes) to x y z w. Exposed for tests.
    std::array<float, 4> unpackQuaternion40(const std::uint8_t* five);

    // Writes the value of each float track (out.size() must be mNumFloatTracks) at `time` seconds, clamped to
    // [0, duration]. Allocates nothing.
    void sampleFloats(const Clip& clip, float time, std::span<float> out);

    // Writes one transform per track (out.size() must be mNumTransformTracks) at `time` seconds, clamped to
    // [0, duration]. Throws nothing; allocates nothing.
    void sample(const Clip& clip, float time, std::span<Transform> out);
}

#endif
