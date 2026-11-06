#ifndef OPENEDEN_COMPAT_MORROWIND_SCENE_OPTIONS_H
#define OPENEDEN_COMPAT_MORROWIND_SCENE_OPTIONS_H

// How Morrowind's NIF files are read (task 0082, answer 3). The reader in components/nif and the scene builder in
// components/nifscene name no game: this pack passes them Morrowind's file versions and Morrowind's root-node rule.
// The dependency points one way only: this pack includes the formats layer, never the other way.

#include <components/nif/niffile.hpp>
#include <components/nifscene/build.hpp>

namespace compat_morrowind
{
    /// Morrowind's versions (4.0.0.0 and 4.0.0.2), not strict, with the rule that the first NiNode of a file loses its
    /// own transform unless it is called "bip01" (any case). Equal to what the reader does by default.
    Nif::ReadOptions morrowindReadOptions();

    /// The scene options of a Morrowind file: morrowindReadOptions() and nothing else changed.
    NifScene::SceneOptions morrowindSceneOptions();
}

#endif
