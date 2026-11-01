#ifndef VSGOPENMW_VIEW_DESCRIPTORS_H
#define VSGOPENMW_VIEW_DESCRIPTORS_H

#include <vsg/core/Array.h>
#include <vsg/core/Data.h>
#include <vsg/state/DescriptorSet.h>

namespace View
{
    /*
     * Creates descriptors required for lighting calculations.
     * @param compute Optionally supports light grid assigment compute shader.
     */
    vsg::Descriptors createLightingDescriptors(vsg::ref_ptr<vsg::Data> lightData, bool compute);

    /*
     * Creates valid, but ineffective descriptors.
     */
    vsg::ref_ptr<vsg::Descriptor> dummyEnvMap();
    vsg::ref_ptr<vsg::Descriptor> dummyShadowMap();
    vsg::ref_ptr<vsg::Descriptor> dummyShadowSampler();
    // OpenEden (task 0028): the light probe buffer: a stand-in with no volume, and the full-size one the game fills.
    vsg::ref_ptr<vsg::Descriptor> dummyProbes();
    vsg::ref_ptr<vsg::Descriptor> probeBuffer(vsg::ref_ptr<vsg::vec4Array>& data);
    // OpenEden (task 0028, stage d): the same for the reflection probes (binding 11).
    vsg::ref_ptr<vsg::Descriptor> dummyReflections();
    vsg::ref_ptr<vsg::Descriptor> reflectionBuffer(vsg::ref_ptr<vsg::vec4Array>& data);

    // openeden-vsg: the opaque depth copy at the far plane, for views without one (soft particles, task 0012, V4k).
    vsg::ref_ptr<vsg::Descriptor> dummyReflectDepth();

    /*
     * Creates VIEW_SET descriptor set.
     * @param descriptors All the descriptors statically used by associated pipelines.
     */
    vsg::ref_ptr<vsg::DescriptorSet> createViewDescriptorSet(const vsg::Descriptors& descriptors);
}

#endif
