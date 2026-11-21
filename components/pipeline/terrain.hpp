#ifndef VSGOPENMW_PIPELINE_TERRAIN_H
#define VSGOPENMW_PIPELINE_TERRAIN_H

#include <vsg/io/Options.h>
#include <vsg/state/GraphicsPipeline.h>
#include <vsg/state/ShaderStage.h>

#include <set>
#include <string>

namespace Pipeline
{
    /// Task 0037: the terrain shader takes rain wetness (needs the material model's pbr look in the rest of the scene).
    struct WetnessOptions
    {
        bool enabled = false;
        bool puddles = false;
        bool debug = false;
    };

    /// Task 0039: the terrain shader takes snow and ash cover.
    struct CoverOptions
    {
        bool enabled = false;
        bool debug = false;
    };

    /// openeden-vsg: with maps, the shaders are compiled with TERRAIN_MAPS and the layout gains the normal map array
    /// and the layer flags (task 0012, V4i). With gbufferOutputs the pass has that many colour outputs beyond the
    /// scene's own, which the terrain writes (task 0026). With pbr (needs maps) the terrain is lit with the material
    /// model and reads the layers' packed material maps, so the layout gains bindings 8 and 9 (task 0044). With maps
    /// and a parallaxQuality (1 low, 2 medium, 3 high) the layers' `_p` height files are marched (bindings 10 and 11,
    /// task 0045). With aoTerrain (and the ambient output, gbufferOutputs of 5) the terrain writes its ambient part
    /// for the ambient occlusion pass instead of zero (task 0048).
    vsg::ref_ptr<vsg::BindGraphicsPipeline> terrain(vsg::ref_ptr<const vsg::Options> shaderOptions, bool maps = false,
        unsigned int gbufferOutputs = 0, bool giSky = false, const WetnessOptions& wetness = {}, bool pbr = false,
        const CoverOptions& cover = {}, unsigned int parallaxQuality = 0, bool aoTerrain = false,
        bool pointShadow = false, bool ambientCube = false);

    /// Task 0072: the defines terrain() compiles both stages with for the same arguments; none when the list is empty.
    /// The set a stage is drawn with is this, cut to the names its `#pragma import_defines` lists, plus the `#define`
    /// lines of the settings files it includes (vsgUtil::stageDefines).
    std::set<std::string> terrainDefines(bool maps, unsigned int gbufferOutputs, bool giSky,
        const WetnessOptions& wetness, bool pbr, const CoverOptions& cover, unsigned int parallaxQuality,
        bool aoTerrain, bool pointShadow = false, bool ambientCube = false);

    /// Task 0363 (stage c): the ids of the terrain's specialization constants (lib/modeconstants.slang), above the
    /// settings table's (Resource::settingsConstantTable) and clear of the object shader's.
    namespace TerrainConstantId
    {
        inline constexpr uint32_t GiSky = 2000;
        inline constexpr uint32_t Wetness = 2001;
        inline constexpr uint32_t WetnessPuddles = 2002;
        inline constexpr uint32_t WetnessDebug = 2003;
        inline constexpr uint32_t Cover = 2004;
        inline constexpr uint32_t CoverDebug = 2005;
        inline constexpr uint32_t AmbientCube = 2006;
        inline constexpr uint32_t PointShadow = 2007;
        inline constexpr uint32_t AoTerrain = 2008;
        inline constexpr uint32_t PomMinSteps = 2009;
        inline constexpr uint32_t PomMaxSteps = 2010;
        inline constexpr uint32_t PomRefineTaps = 2011;
    }

    /// Task 0363 (stage c): the values of those constants for the same arguments as terrainDefines: a flag is true
    /// exactly when the define would be present, and the three step counts are those of lib/material/parallax.slang
    /// for the quality (medium when none). Given to the terrain's MODE_CONSTANTS permutation (`[Rendering] shader
    /// constants`); the CPU works out everything derived, the shader branches on each constant alone.
    vsg::ShaderStage::SpecializationConstants terrainConstants(bool maps, unsigned int gbufferOutputs, bool giSky,
        const WetnessOptions& wetness, bool pbr, const CoverOptions& cover, unsigned int parallaxQuality,
        bool aoTerrain, bool pointShadow = false, bool ambientCube = false);
}

#endif
