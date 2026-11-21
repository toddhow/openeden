#ifndef OPENEDEN_PIPELINE_OBJECTCONSTANTS_H
#define OPENEDEN_PIPELINE_OBJECTCONSTANTS_H

#include <vsg/state/ShaderStage.h>

#include <cstdint>
#include <set>
#include <string>

namespace Pipeline
{
    /// Task 0363 (stage d): the ids of the object shader's own specialization constants (lib/objectconstants.slang).
    /// The ones it shares with the terrain (the wetness, cover, sky field, ambient cube, point shadow and parallax
    /// numbers) keep Pipeline::TerrainConstantId's ids (lib/modeconstants.slang); these are above them and clear of the
    /// settings table (Resource::settingsConstantTable).
    namespace ObjectConstantId
    {
        inline constexpr uint32_t Gi = 2020;
        inline constexpr uint32_t PerPixelLighting = 2021;
        inline constexpr uint32_t PbrLit = 2022;
        inline constexpr uint32_t PbrPhysical = 2023;
        inline constexpr uint32_t WetLit = 2024;
        inline constexpr uint32_t CoverLit = 2025;
        inline constexpr uint32_t LodFade = 2026;
        inline constexpr uint32_t AdjustCoverage = 2027;
        inline constexpr uint32_t LodSway = 2028; // the vertex stage's
    }

    /// The values of the fragment stage's constants for the stage's define set (the names Pipeline::getDefine gives the
    /// requested modes). A plain flag is on exactly when its define is; the derived ones (per pixel lighting, the
    /// material model's lit surfaces, wetness and cover on them) are worked out here, because the shader branches on
    /// each constant alone (an expression over constants is an OpSpecConstantOp, files/shaders/CLAUDE.md). The three
    /// parallax step counts are those of lib/material/parallax.slang for the quality.
    vsg::ShaderStage::SpecializationConstants objectFragmentConstants(const std::set<std::string>& defines);

    /// The vertex stage's: the far tree sway.
    vsg::ShaderStage::SpecializationConstants objectVertexConstants(const std::set<std::string>& defines);
}

#endif
