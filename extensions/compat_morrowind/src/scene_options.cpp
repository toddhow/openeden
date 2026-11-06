#include "compat_morrowind/scene_options.hpp"

namespace compat_morrowind
{
    Nif::ReadOptions morrowindReadOptions()
    {
        Nif::ReadOptions options;
        options.mAllowedVersions = Nif::VersionSet::Morrowind;
        options.mStrict = false;
        options.mMorrowindRootRule = true;
        options.mRootRuleExemptName = "bip01";
        return options;
    }

    NifScene::SceneOptions morrowindSceneOptions()
    {
        NifScene::SceneOptions options;
        options.mRead = morrowindReadOptions();
        return options;
    }
}
