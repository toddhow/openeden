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
    // OpenEden (task 0036): the exterior sky-visibility field: the cells' data (binding 12) and its small header
    // (binding 13), as stand-ins that switch it off, and the full-size ones the game fills.
    vsg::ref_ptr<vsg::Descriptor> dummySkyVis();
    vsg::ref_ptr<vsg::Descriptor> skyVisBuffer(vsg::ref_ptr<vsg::vec4Array>& data);
    vsg::ref_ptr<vsg::Descriptor> dummySkyVisSky();
    vsg::ref_ptr<vsg::Descriptor> skyVisSkyBuffer(vsg::ref_ptr<vsg::vec4Array>& data);

    // OpenEden (task 0041): the volumetric fog's volume (binding 14) and parameter block (binding 15). The stand-ins
    // hold a volume of one clear texel and parameters that say "off"; `volumeParamsBuffer` is the game's dynamic block.
    vsg::ref_ptr<vsg::Descriptor> dummyVolume();
    vsg::ref_ptr<vsg::Descriptor> dummyVolumeParams();
    vsg::ref_ptr<vsg::Descriptor> volumeParamsBuffer(vsg::ref_ptr<vsg::vec4Array>& data);

    // OpenEden (task 0042): the volumetric clouds' ground shadow map (binding 16). The stand-in is one texel; the
    // shader reads its size and finds no map (so no shadow) in it.
    vsg::ref_ptr<vsg::Descriptor> dummyCloudShadow();

    // OpenEden (task 0077): the point lights' shadow maps (binding 17: a depth 2D array), their comparison sampler
    // (18) and the data buffer (19). The stand-ins hold a map of one texel and a buffer with no slots (the shader reads
    // a slot count of 0 and finds no shadow); the game's real ones come from View::PointShadow.
    vsg::ref_ptr<vsg::Descriptor> dummyPointShadowMap();
    vsg::ref_ptr<vsg::Descriptor> dummyPointShadowSampler();
    vsg::ref_ptr<vsg::Descriptor> dummyPointShadowData();

    // openeden-vsg: the opaque depth copy at the far plane, for views without one (soft particles, task 0012, V4k).
    vsg::ref_ptr<vsg::Descriptor> dummyReflectDepth();

    /*
     * Creates VIEW_SET descriptor set.
     * @param descriptors All the descriptors statically used by associated pipelines.
     */
    vsg::ref_ptr<vsg::DescriptorSet> createViewDescriptorSet(const vsg::Descriptors& descriptors);
}

#endif
