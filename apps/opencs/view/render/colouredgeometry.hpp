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
    struct ColouredOptions
    {
        bool mTwoSided = true;
        /// Over everything, for the overlay bin: no depth test, no depth written.
        bool mNoDepth = false;
        /// Vertex alpha blends, and no depth is written.
        bool mBlend = false;
        /// Rays can hit its triangles. A tool's helper that must never stand in the way of a click turns this off.
        bool mPickable = true;
    };

    /// Unlit lines or triangles with a colour per vertex, as the OSG editor drew its guides with lighting off. Not
    /// compiled: that is the caller's.
    vsg::ref_ptr<vsg::Node> createColouredGeometry(const Pipeline::Builder& builder, VkPrimitiveTopology topology,
        vsg::ref_ptr<vsg::vec3Array> vertices, vsg::ref_ptr<vsg::vec4Array> colours,
        vsg::ref_ptr<vsg::ushortArray> indices = {}, bool twoSided = true);

    vsg::ref_ptr<vsg::Node> createColouredGeometry(const Pipeline::Builder& builder, VkPrimitiveTopology topology,
        vsg::ref_ptr<vsg::vec3Array> vertices, vsg::ref_ptr<vsg::vec4Array> colours, vsg::ref_ptr<vsg::Data> indices,
        const ColouredOptions& options);

    /// The state for a draw the caller made, because it fills the draw's arrays later or draws them in parts.
    vsg::ref_ptr<vsg::Node> wrapColouredDraw(const Pipeline::Builder& builder, VkPrimitiveTopology topology,
        vsg::ref_ptr<vsg::Node> draw, const ColouredOptions& options);
}

#endif
