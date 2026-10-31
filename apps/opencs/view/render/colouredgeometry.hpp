#ifndef OPENCS_VIEW_COLOUREDGEOMETRY_H
#define OPENCS_VIEW_COLOUREDGEOMETRY_H

#include <vsg/core/Array.h>
#include <vsg/nodes/Node.h>
#include <vsg/vk/vulkan.h>

namespace Pipeline
{
    class Builder;
}

namespace CSVRender
{
    /// Unlit lines or triangles with a colour per vertex, as the OSG editor drew its guides with lighting off. Not compiled:
    /// that is the caller's. Takes no part in intersection tests, as stage C6 will decide for itself.
    vsg::ref_ptr<vsg::Node> createColouredGeometry(const Pipeline::Builder& builder, VkPrimitiveTopology topology,
        vsg::ref_ptr<vsg::vec3Array> vertices, vsg::ref_ptr<vsg::vec4Array> colours,
        vsg::ref_ptr<vsg::ushortArray> indices = {}, bool twoSided = true);
}

#endif
