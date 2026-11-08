#ifndef OPENEDEN_COMPAT_SKYRIMSE_SCENE_OPTIONS_H
#define OPENEDEN_COMPAT_SKYRIMSE_SCENE_OPTIONS_H

// How Skyrim Special Edition's NIF files are read (task 0086, as task 0082 answer 3 did for Morrowind): the reader in
// components/nif and the scene builder in components/nifscene name no game; this pack hands them Skyrim SE's file
// versions (20.2.0.7, user version 12, stream version 100) and says that Morrowind's root-node rule does not apply.

#include <components/nif/niffile.hpp>
#include <components/nifscene/build.hpp>

#include <string_view>

namespace compat_skyrimse
{
    /// Skyrim SE's versions (the set also accepts Morrowind's), not strict, without Morrowind's root-node rule.
    Nif::ReadOptions skyrimReadOptions();

    /// The scene options of a Skyrim SE file: skyrimReadOptions(), zero-weight influences dropped.
    NifScene::SceneOptions skyrimSceneOptions();

    /// The humanoid skeleton the game gives male actors and the one it gives female actors, as VFS paths. From recall
    /// of the game's layout; the real-data check lists whether the installed archives hold them.
    std::string_view defaultMaleSkeleton();
    std::string_view defaultFemaleSkeleton();
}

#endif
