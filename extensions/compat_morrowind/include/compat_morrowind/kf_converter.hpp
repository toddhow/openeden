#pragma once

#include <openeden/contracts/kf_converter_contract.hpp>

#include "kf_animation.hpp"
#include <vsg/maths/quat.h>
#include <vsg/maths/vec3.h>

#include <string>
#include <vector>

namespace compat_morrowind
{
    struct BoneInfo
    {
        std::string name;
        int parent = -1;
        std::vector<int> children;
        vsg::vec3 restTranslation{ 0.0f, 0.0f, 0.0f };
        vsg::dquat restRotation{ 0.0f, 0.0f, 0.0f, 1.0f };
        float restScale = 1.0f;
    };

    class KfConverter : public openeden::contracts::KfConverterContract
    {
    public:
        ConversionResult convertKfToGltf(
            std::span<const std::uint8_t> kfBytes,
            std::span<const std::uint8_t> skeletonNifBytes,
            const ConversionOptions& options = {}) override;

        ConversionResult convertParsedKfToGltf(
            const KfAnimation& animation,
            const std::vector<BoneInfo>& bones,
            const ConversionOptions& options = {});
    };
} // namespace compat_morrowind
