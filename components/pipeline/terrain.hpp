#ifndef VSGOPENMW_PIPELINE_TERRAIN_H
#define VSGOPENMW_PIPELINE_TERRAIN_H

#include <vsg/io/Options.h>
#include <vsg/state/GraphicsPipeline.h>

namespace Pipeline
{
    /// openeden-vsg: with maps, the shaders are compiled with TERRAIN_MAPS and the layout gains the normal map array and
    /// the layer flags (task 0012, V4i).
    vsg::ref_ptr<vsg::BindGraphicsPipeline> terrain(vsg::ref_ptr<const vsg::Options> shaderOptions, bool maps = false);
}

#endif
