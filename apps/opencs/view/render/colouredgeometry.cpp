#include "colouredgeometry.hpp"

#include <vsg/commands/BindIndexBuffer.h>
#include <vsg/nodes/Geometry.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexDraw.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/state/ArrayState.h>

#include <components/pipeline/builder.hpp>
#include <components/pipeline/graphics.hpp>

namespace
{
    // The default shader's unlit colour mode, as Render::DebugGeometry.
    constexpr uint32_t sUnlitColorMode = 3;
}

vsg::ref_ptr<vsg::Node> CSVRender::wrapColouredDraw(const Pipeline::Builder& builder, VkPrimitiveTopology topology,
    vsg::ref_ptr<vsg::Node> draw, const ColouredOptions& colouredOptions)
{
    Pipeline::Options options;
    options.shader.addMode(Pipeline::Mode::VERTEX);
    options.shader.addMode(Pipeline::Mode::COLOR);
    options.colorMode = sUnlitColorMode;
    options.primitiveTopology = topology;
    if (colouredOptions.mTwoSided)
        options.cullMode = VK_CULL_MODE_NONE;
    if (colouredOptions.mNoDepth)
    {
        options.depthTest = false;
        options.depthWrite = false;
    }
    if (colouredOptions.mBlend)
    {
        options.blend = true;
        options.depthWrite = false;
        // Destination alpha is left as the scene wrote it, as Render::DebugGeometry.
        options.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        options.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    }

    auto state = vsg::StateGroup::create();
    state->stateCommands = { builder.graphics->getOrCreate(options) };
    if (!colouredOptions.mPickable)
        state->prototypeArrayState = vsg::NullArrayState::create();
    state->addChild(draw);
    return state;
}

vsg::ref_ptr<vsg::Node> CSVRender::createColouredGeometry(const Pipeline::Builder& builder, VkPrimitiveTopology topology,
    vsg::ref_ptr<vsg::vec3Array> vertices, vsg::ref_ptr<vsg::vec4Array> colours, vsg::ref_ptr<vsg::Data> indices,
    const ColouredOptions& options)
{
    if (indices)
    {
        auto draw = vsg::VertexIndexDraw::create();
        draw->assignArrays({ vertices, colours });
        draw->assignIndices(indices);
        draw->indexCount = static_cast<uint32_t>(indices->valueCount());
        draw->instanceCount = 1;
        return wrapColouredDraw(builder, topology, draw, options);
    }
    auto draw = vsg::VertexDraw::create();
    draw->assignArrays({ vertices, colours });
    draw->vertexCount = static_cast<uint32_t>(vertices->size());
    draw->instanceCount = 1;
    return wrapColouredDraw(builder, topology, draw, options);
}

vsg::ref_ptr<vsg::Node> CSVRender::createColouredGeometry(const Pipeline::Builder& builder, VkPrimitiveTopology topology,
    vsg::ref_ptr<vsg::vec3Array> vertices, vsg::ref_ptr<vsg::vec4Array> colours, vsg::ref_ptr<vsg::ushortArray> indices,
    bool twoSided)
{
    ColouredOptions options;
    options.mTwoSided = twoSided;
    return createColouredGeometry(builder, topology, vertices, colours, vsg::ref_ptr<vsg::Data>(indices), options);
}
