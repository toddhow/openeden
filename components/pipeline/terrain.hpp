#ifndef VSGOPENMW_PIPELINE_TERRAIN_H
#define VSGOPENMW_PIPELINE_TERRAIN_H

#include <vsg/io/Options.h>
#include <vsg/state/GraphicsPipeline.h>

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
        const CoverOptions& cover = {}, unsigned int parallaxQuality = 0, bool aoTerrain = false);
}

#endif
