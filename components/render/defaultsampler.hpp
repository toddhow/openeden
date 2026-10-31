#ifndef VSGOPENMW_RENDER_DEFAULTSAMPLER_H
#define VSGOPENMW_RENDER_DEFAULTSAMPLER_H

#include <vsg/core/ref_ptr.h>

namespace vsg
{
    class PhysicalDevice;
    class Sampler;
}

namespace Render
{
    /// The texture sampler the [General] settings describe, within what the device supports.
    vsg::ref_ptr<vsg::Sampler> createDefaultSampler(vsg::ref_ptr<vsg::PhysicalDevice> physicalDevice, bool supportsAnisotropy);
}

#endif
