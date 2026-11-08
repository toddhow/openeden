#include "compat_skyrimse/scene_options.hpp"

namespace compat_skyrimse
{
    Nif::ReadOptions skyrimReadOptions()
    {
        Nif::ReadOptions options;
        options.mAllowedVersions = Nif::VersionSet::SkyrimSE;
        options.mStrict = false;
        options.mMorrowindRootRule = false;
        return options;
    }

    NifScene::SceneOptions skyrimSceneOptions()
    {
        NifScene::SceneOptions options;
        options.mRead = skyrimReadOptions();
        options.mKeepZeroWeights = false;
        return options;
    }

    // From recall of the game's data layout (RECALLED-VERIFY); the install-in-place check
    // (apps/components_tests/skyrimse/testskyrimserealdata.cpp) reports whether the installed archives hold them.
    std::string_view defaultMaleSkeleton()
    {
        return "meshes/actors/character/character assets/skeleton.nif";
    }

    std::string_view defaultFemaleSkeleton()
    {
        return "meshes/actors/character/character assets female/skeleton_female.nif";
    }
}
