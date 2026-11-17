#ifndef OPENEDEN_COMPONENTS_HAVOKPACK_PAIRED_H
#define OPENEDEN_COMPONENTS_HAVOKPACK_PAIRED_H

// Task 0267: paired (two actor) animations. One clip file animates two skeletons at once: its annotation tracks name
// the bone of each transform track (provenance F54), a track named "PairedRoot" belongs to neither actor, a name that
// starts with "2_" belongs to the second actor (the horse of a mount, the victim of a kill move) and every other name
// to the first (F55). Every actor plays the tracks that name its own bones and ignores the rest.
//
// The first actor's root tracks place him in a frame the clip names (the second actor's, for a mount: F56), so what a
// game needs of a clip is where its first actor starts and ends in it: `describe`.
//
// Formats layer, standard library only. Nothing here allocates per frame; bindSide allocates once, describe allocates
// its scratch once.

#include "idle.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace HavokPack::Paired
{
    enum class Side : std::uint8_t
    {
        Shared = 0, ///< the clip's own root ("PairedRoot")
        First = 1, ///< the actor that started it, and every name with no prefix
        Second = 2, ///< the other actor: names with the "2_" prefix
    };

    struct TrackRole
    {
        Side mSide = Side::First;
        /// The bone the track drives, lower case, the prefix removed; empty when the track names no bone (an empty
        /// name, the Shared root, or a name that is only the prefix).
        std::string mBone;
    };

    /// Which actor a track belongs to and the bone it drives. Case-insensitive; never throws.
    TrackRole classifyTrack(std::string_view name);

    /// A clip a renderer can load: which side's tracks to play and where the file is.
    struct ClipRef
    {
        Side mSide = Side::First;
        /// A VFS path: "meshes/<path>.hkx", lower case, forward slashes.
        std::string mPath;
    };

    /// The clip name a game gives the animation loader for a side of a paired clip: "~1/<path>" or "~2/<path>", the
    /// path under meshes/ without the extension (for example "actors/sharedkillmoves/human&horse/paired_mount").
    std::string clipName(Side side, std::string_view pathUnderMeshes);

    /// The inverse; nothing for a name that does not start with "~1/" or "~2/", for an empty path, for a path with a
    /// ".." segment, a drive letter or a leading slash, and for a path longer than 240 bytes.
    std::optional<ClipRef> parseClipName(std::string_view name);

    /// The clip with `mTrackBones[i]` set to the bone of track i when the track belongs to `side` and to "" otherwise.
    /// Throws Error when the clip has no track names (it is not a paired clip).
    BoundClip bindSide(Clip clip, Side side);

    /// A place and a turn about Z (counter-clockwise seen from above, radians), in the frame the clip started in.
    struct RootPose
    {
        double mX = 0;
        double mY = 0;
        double mZ = 0;
        double mYaw = 0;
    };

    struct Description
    {
        float mDuration = 0;
        RootPose mStart;
        RootPose mEnd;
    };

    /// Where the first actor's root is at the start and at the end of the clip: the first actor's tracks "NPC" (when
    /// the clip has it) and "NPC Root [Root]" composed. Nothing when the clip has no such root track (it needs track
    /// names) or a value is not finite.
    std::optional<Description> describe(const Clip& clip);

    /// Task 0278: where the SECOND actor's own root node (the "2_" track, which names no bone) is at the start and at
    /// the end, in the same frame (a kill move's victim stands at the clip's origin composed with it: the bear's is
    /// 157 units in front, F58). Nothing when the clip has no such track, no track names, or a value is not finite.
    std::optional<Description> describeSecond(const Clip& clip);
}

#endif
