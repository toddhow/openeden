#ifndef OPENEDEN_COMPONENTS_HAVOKPACK_IDLE_H
#define OPENEDEN_COMPONENTS_HAVOKPACK_IDLE_H

// Task 0132 (M-S3, first slice): what a game needs to loop a decoded idle clip on a skeleton it already has as a
// scene graph: where the files are (derived from the actor's skeleton model path), a clip bound to bone NAMES (the
// scene's nodes carry the names), and the looping time. Pure; standard library only. Formats layer. Root motion is
// not part of it.

#include "animation.hpp"
#include "skeleton.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace HavokPack
{
    // "meshes/actors/character/character assets/skeleton.nif" (any case, either slash, with or without "meshes/")
    // to "meshes/actors/character/character assets/skeleton.hkx". Empty when the path is not a .nif.
    std::string skeletonHkxPath(std::string_view skeletonNifPath);

    // The clip file `clipName` beside the actor's skeleton folder: "<actor folder>/animations/<clipName>.hkx", with
    // the actor folder being the skeleton model's folder's parent. Lower case, forward slashes, "meshes/" first.
    // Empty when the path has no such parent.
    std::string clipHkxPath(std::string_view skeletonNifPath, std::string_view clipName = "mt_idle");

    // A clip with the lower-case name of the bone each track drives.
    struct BoundClip
    {
        Clip mClip;
        std::vector<std::string> mTrackBones;
    };

    // Names the bone of every track from the skeleton. Throws Error when a track names a bone the skeleton does not
    // have (the clip belongs to another skeleton).
    BoundClip bindClip(const Skeleton& skeleton, Clip clip);

    // `time` wrapped into [0, duration). 0 for a duration that is not positive or a time that is not finite.
    float loopTime(float time, float duration);

    // Every track at `time` wrapped by loopTime. out.size() must equal mNumTransformTracks, else nothing is written.
    // Allocates nothing.
    void samplePose(const BoundClip& clip, float time, std::span<Transform> out);
}

#endif
