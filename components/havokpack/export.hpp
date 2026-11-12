#ifndef OPENEDEN_COMPONENTS_HAVOKPACK_EXPORT_H
#define OPENEDEN_COMPONENTS_HAVOKPACK_EXPORT_H

// Task 0119: a decoded clip and its skeleton written as glTF 2.0 (one node per bone, one animation sampled at every
// frame), and world-space bone positions for looking at a pose. Standard library only. The output is derived from the
// user's own files and is never committed.
//
// Units and axes are the file's own (Havok units, Z up, as stored); the glTF carries them as written and says so in
// the asset's extras. Nothing here rotates the data into glTF's Y-up convention.

#include "animation.hpp"
#include "skeleton.hpp"

#include <span>
#include <string>
#include <vector>

namespace HavokPack
{
    // Transform of each bone in the skeleton's root frame, from local transforms in bone order. Parents come first
    // (the skeleton reader guarantees it). `local.size()` must equal the bone count or Error is thrown.
    std::vector<Transform> toWorld(const Skeleton& skeleton, std::span<const Transform> local);

    // The local transforms of every bone at `time`: the reference pose with the clip's tracks written over it. A bone
    // with no track keeps its reference pose. Throws Error if a track names a bone the skeleton does not have.
    std::vector<Transform> poseAt(const Skeleton& skeleton, const Clip& clip, float time);

    // A complete .gltf file (JSON with the buffer embedded as a base64 data URI). One animation named `name`; a channel
    // for the translation, rotation and scale of each track's bone, sampled at every frame. Throws Error on a clip
    // that does not fit the skeleton.
    std::string writeGltf(const Skeleton& skeleton, const Clip& clip, const std::string& name);
}

#endif
