#ifndef CSM_MESH_ANIMATIONKEYS_H
#define CSM_MESH_ANIMATIONKEYS_H

#include <string>
#include <string_view>
#include <vector>

namespace CSMMesh
{
    struct TextKey
    {
        float time = 0.f;
        std::string text;
    };

    struct AnimationGroup
    {
        std::string name;
        float start = 0.f;
        float stop = 0.f;
        std::vector<TextKey> keys;
    };

    /// Groups text keys as Anim::Tags::emplace does (components/animation/tags.hpp): "Group: key" belongs to Group,
    /// split at the first ": "; keys without it are skipped. Groups in order of first appearance; keys sorted by
    /// time (stable); start and stop are the smallest and largest key time.
    std::vector<AnimationGroup> groupTextKeys(const std::vector<TextKey>& keys);

    /// The animation file the engine pairs with a model (components/resource/resourcesystem.cpp,
    /// createAnimationOptions): only a model whose file name starts with x or X and ends in .nif, with .kf in its
    /// place. Empty otherwise.
    std::string kfSidecarFor(std::string_view modelPath);
}

#endif
