#ifndef OPENEDEN_COMPONENTS_HAVOKPACK_ANIMATIONDATA_H
#define OPENEDEN_COMPONENTS_HAVOKPACK_ANIMATIONDATA_H

// Task 0119 (stage 7): the plain-text sidecars of Skyrim Special Edition's animation archive, read strictly.
//
//   meshes/animationdata/<project>.txt            a project's file list and its clip generators with their events
//   meshes/animationdata/boundanims/anims_<project>.txt   root motion of each animation, keyed by cache index
//
// Every layout fact is logged in docs/research/0054-provenance.md (F37 to F52); the rules the readers enforce are the
// ones that hold in all of the player's installed files (323 project files, 39 motion files). Formats layer,
// standard library only. A reader owns what it returns; results are immutable, so concurrent reads are safe.
// Line ends: every installed file uses CR LF; a lone LF is accepted too (not seen in the data).

#include "packfile.hpp" // HavokPack::Error

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace HavokPack
{
    // Caps checked before anything is allocated. Far above the real files (the largest has 21,051 lines).
    struct TextLimits
    {
        std::size_t mMaxFileBytes = 64u * 1024u * 1024u;
        std::uint32_t mMaxLineLength = 4096;
        std::uint32_t mMaxFiles = 4096; // behaviour, character and skeleton files a project lists
        std::uint32_t mMaxClips = 1u << 16;
        std::uint32_t mMaxEventsPerClip = 1u << 12;
        std::uint32_t mMaxBlocks = 1u << 16;
        std::uint32_t mMaxRowsPerBlock = 1u << 16;
    };

    struct ClipEvent
    {
        std::string mName;
        // As stored: seconds of playback time, i.e. animation time divided by the generator's playback speed
        // (negative when the speed is). Use eventAnimationTime() for the time inside the animation.
        float mTime = 0;
    };

    // One clip generator of a project: a name, the index of its animation in the project's animation list, the
    // playback speed and the events fired during playback.
    struct SidecarClip
    {
        std::string mName;
        std::uint32_t mCacheIndex = 0;
        float mPlaybackSpeed = 1;
        // The two numbers that follow the speed, kept as stored. Their meaning is not established (mostly 0 0).
        float mUnknownA = 0;
        float mUnknownB = 0;
        std::vector<ClipEvent> mEvents;
    };

    struct AnimationProject
    {
        std::vector<std::string> mFiles; // the behaviour, character and skeleton files, as written (backslashes)
        std::vector<SidecarClip> mClips; // empty when the project stores none
    };

    // Throws Error with the line number in the text ("... (line 12)").
    AnimationProject readAnimationProject(std::string_view text, const TextLimits& limits = {});

    // Root motion of one animation: the displacement and the rotation reached at each listed time, measured from the
    // start of the animation (at time 0 both are zero / identity: no row is stored there).
    struct MotionBlock
    {
        std::uint32_t mCacheIndex = 0;
        float mDuration = 0;
        std::vector<std::array<float, 4>> mTranslation; // time, x, y, z
        std::vector<std::array<float, 5>> mRotation; // time, x, y, z, w
    };

    struct MotionTable
    {
        std::vector<MotionBlock> mBlocks; // in file order
        // The block with this cache index, or nullptr.
        const MotionBlock* find(std::uint32_t cacheIndex) const;
    };

    // Throws Error with the line number.
    MotionTable readMotionTable(std::string_view text, const TextLimits& limits = {});

    // Displacement / rotation at `time` seconds: linear between the listed times, from zero / identity at 0, held
    // after the last row. The rotation is normalised. Allocates nothing.
    std::array<float, 3> rootTranslationAt(const MotionBlock& block, float time);
    std::array<float, 4> rootRotationAt(const MotionBlock& block, float time); // x y z w

    // The event's time inside the animation: the stored time multiplied by the playback speed.
    float eventAnimationTime(const SidecarClip& clip, const ClipEvent& event);

    // What a clip file (stem + duration) is linked to. The sidecars never name the .hkx a generator plays, so the link
    // is by name: a generator whose name equals the file's stem (case-insensitive, a trailing ".hkx" ignored) and
    // whose animation's root-motion duration equals `duration` (relative tolerance 1e-3; a generator whose animation
    // has no motion block cannot be checked and is kept). Root motion belongs to the file, events to each generator.
    struct ClipLink
    {
        std::vector<const SidecarClip*> mClips; // every generator accepted, in file order
        const MotionBlock* mMotion = nullptr; // the block of the first accepted generator that has one
    };
    ClipLink linkClip(
        const AnimationProject& project, const MotionTable& motion, std::string_view fileStem, float duration);
}

#endif
