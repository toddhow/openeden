#ifndef VSGOPENMW_PIPELINE_TERRAIN_H
#define VSGOPENMW_PIPELINE_TERRAIN_H

#include <vsg/io/Options.h>
#include <vsg/state/GraphicsPipeline.h>

namespace Pipeline
{
    /// openeden-vsg: with maps, the shaders are compiled with TERRAIN_MAPS and the layout gains the normal map array
    /// and the layer flags (task 0012, V4i). With gbufferOutputs the pass has that many colour outputs beyond the
    /// scene's own, which the terrain writes (task 0026).
    vsg::ref_ptr<vsg::BindGraphicsPipeline> terrain(vsg::ref_ptr<const vsg::Options> shaderOptions, bool maps = false,
        unsigned int gbufferOutputs = 0, bool giSky = false);
}

#endif
