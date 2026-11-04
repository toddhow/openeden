#ifndef VSGOPENMW_PIPELINE_OPTIONS_H
#define VSGOPENMW_PIPELINE_OPTIONS_H

#include <compare>
#include <map>
#include <string>

#include <vsg/vk/vulkan.h>

#include "mode.hpp"

namespace Pipeline
{
    /// Task 0072, stage h (review round 4, finding 10): the colour mode of a particle system that has no
    /// NiVertexColorProperty. Modes 0 (the NIF says "ignore the vertex colour", or lighting off), 1 (emissive), 2
    /// (ambient and diffuse) and 3 (unlit debug geometry) are the property's; OpenMW 0.51 gave a particle system
    /// without the property the vertex colour as ambient and diffuse (nifloader.cpp handleParticleSystem passes
    /// hasVertexColors = true). The GLSL switches only on 1, 2 and 3, so it treats this value as 0; the Slang twin
    /// (lib/material/terms.slang) treats it as 2, keeping the particle's own colour and its alpha over its life.
    inline constexpr uint32_t particleOwnColorMode = 4;

    /// The colour mode a particle system gets: its NiVertexColorProperty's, or particleOwnColorMode without one.
    inline constexpr uint32_t colorModeOfParticles(bool hasVertexColorProperty, uint32_t propertyColorMode)
    {
        return hasVertexColorProperty ? propertyColorMode : particleOwnColorMode;
    }

    /*
     * Affects shader source.
     */
    struct ShaderOptions
    {
        std::string path;
        std::vector<bool> modes;
        /*auto*/ std::strong_ordering operator<=>(const ShaderOptions&) const = default;

        void addMode(Mode mode) { Pipeline::addMode(mode, modes); }
        void removeMode(Mode mode) { Pipeline::removeMode(mode, modes); }
        bool hasMode(Mode mode) const { return Pipeline::hasMode(mode, modes); }
    };

    /*
     * Configures graphics pipeline.
     */
    struct Options
    {
        ShaderOptions shader{};
        uint32_t numUvSets = 0;
        std::map<uint32_t, uint32_t> nonStandardUvSets{};
        VkPrimitiveTopology primitiveTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        bool depthTest = true;
        bool depthWrite = true;
        bool specular = false;
        bool blend = false;
        bool colorWrite = true;
        bool depthBias = false;
        VkBlendFactor srcBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        VkBlendFactor dstBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        VkBlendFactor srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        VkBlendFactor dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        uint32_t alphaTestMode = 0;
        VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
        VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
        uint32_t colorMode = 0;

        /*auto*/ std::strong_ordering operator<=>(const Options&) const = default;
    };
}

#endif
