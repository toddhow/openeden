#include "particle.hpp"

#include "particledescriptors.hpp"

#include <components/vsgutil/readshader.hpp>
#include <components/vsgutil/shaderpermutation.hpp>

namespace Pipeline
{
    Particle::Particle(vsg::ref_ptr<const vsg::Options> readShaderOptions)
        : mShaderOptions(readShaderOptions)
    {
        VkShaderStageFlags computeStage = VK_SHADER_STAGE_COMPUTE_BIT;

        vsg::DescriptorSetLayoutBindings descriptorBindings{
            { Descriptors::PARTICLE_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, computeStage, nullptr },
            { Descriptors::SIMULATE_ARGS_BINDING, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, computeStage, nullptr },
            { Descriptors::FRAME_ARGS_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, computeStage, nullptr },
            { Descriptors::COLOR_CURVE_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, computeStage, nullptr }
        };
        static const auto nullDsl = vsg::DescriptorSetLayout::create();
        vsg::DescriptorSetLayouts descriptorSetLayouts
            = { nullDsl, nullDsl, vsg::DescriptorSetLayout::create(descriptorBindings) };

        mLayout = vsg::PipelineLayout::create(descriptorSetLayouts, vsg::PushConstantRanges());
    }

    Particle::~Particle() {}

    vsg::ref_ptr<vsg::BindComputePipeline> Particle::create(ParticleModeFlags modes) const
    {
        // Task 0072, stage c: the Slang twin when `[Rendering] slang shaders` is on and it was built.
        const std::string path = "comp/particle/simulate.comp";
        auto shaderStage = vsgUtil::withSlangTwin(vsgUtil::readShader(path, mShaderOptions), path, mShaderOptions);
        shaderStage = vsg::ShaderStage::create(*shaderStage);
        shaderStage->specializationConstants = { { 0, vsg::Value<VkBool32>::create(modes & ParticleMode_Size) },
            { 1, vsg::Value<VkBool32>::create(modes & ParticleMode_Color) },
            { 2, vsg::Value<VkBool32>::create(modes & ParticleMode_GravityPlane) },
            { 3, vsg::Value<VkBool32>::create(modes & ParticleMode_GravityPoint) },
            { 4, vsg::Value<VkBool32>::create(modes & ParticleMode_CollidePlane) },
            { 5, vsg::Value<VkBool32>::create(modes & ParticleMode_WorldSpace) } };
        // A twin takes the `*.settings` values as specialization constants too (none are read today; no-op for GLSL).
        vsgUtil::addSettingsConstants(*shaderStage, mShaderOptions.get());
        return vsg::BindComputePipeline::create(vsg::ComputePipeline::create(mLayout, shaderStage));
    }
}
