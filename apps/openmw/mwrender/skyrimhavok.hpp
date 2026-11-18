#ifndef OPENMW_MWRENDER_SKYRIMHAVOK_HPP
#define OPENMW_MWRENDER_SKYRIMHAVOK_HPP

// Task 0289 (dungeon mechanics): Skyrim's animated objects (puzzle pillars, ring doors, portcullises, levers) are
// driven by Havok behaviour graphs, not by NIF sequences: the model has a node per bone (a NiTransformController
// with no data) and the clips are .hkx files named by meshes/animationdata/<model>.txt. This turns each clip of such
// an object into a NifScene::Sequence named by the events the game's scripts call it by (components/havokpack/
// objectproject.hpp), so that SkyrimSequence plays it with the machinery of the doors and lids. The behaviour graph
// itself is not read.
//
// The clip's bone tracks are sampled every kKeyStep seconds into linear keys on the NIF node of the same name (ASCII
// case-insensitive). A bone with no node is skipped. Havok clip translations are taken as game units, as the actor
// clips are.
//
// Threads: pure and reentrant; called from the loader (SkyrimSequence::loadModel, under its cache).

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include <components/havokpack/skeleton.hpp>
#include <components/nifscene/scene.hpp>
#include <components/vfs/manager.hpp>

namespace MWRender::SkyrimHavok
{
    inline constexpr float kKeyStep = 1.f / 30.f;
    inline constexpr float kMaxClipSeconds = 30.f;
    inline constexpr std::size_t kMaxSequences = 32;

    using Sampler = std::function<void(float time, std::span<HavokPack::Transform> out)>;

    /// One sequence from a sampled clip. `nodeNames[i]` is the NIF node track i drives (an empty name skips the
    /// track). A duration that is not finite, not positive or over kMaxClipSeconds gives a sequence with no tracks.
    [[nodiscard]] NifScene::Sequence sequenceFromSampler(std::string name, float duration,
        const std::vector<std::string>& nodeNames, const Sampler& sampler);

    /// Adds the sequences of the model's Havok project to the scene (one per event name of each clip, none for a name
    /// the scene already has). Returns how many were added; 0 when the model has no project or anything fails (the
    /// reason goes to the log at Verbose level). Never throws.
    std::size_t addSequences(NifScene::Scene& scene, const VFS::Manager& vfs, const std::string& modelPath);
}

#endif
