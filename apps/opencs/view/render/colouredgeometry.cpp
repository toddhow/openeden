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

vsg::ref_ptr<vsg::Node> CSVRender::createColouredGeometry(const Pipeline::Builder& builder, VkPrimitiveTopology topology,
    vsg::ref_ptr<vsg::vec3Array> vertices, vsg::ref_ptr<vsg::vec4Array> colours, vsg::ref_ptr<vsg::ushortArray> indices,
    bool twoSided)
{
    Pipeline::Options options;
    options.shader.addMode(Pipeline::Mode::VERTEX);
    options.shader.addMode(Pipeline::Mode::COLOR);
    options.colorMode = sUnlitColorMode;
    options.primitiveTopology = topology;
    if (twoSided)
        options.cullMode = VK_CULL_MODE_NONE;

    auto state = vsg::StateGroup::create();
    state->stateCommands = { builder.graphics->getOrCreate(options) };
    state->prototypeArrayState = vsg::NullArrayState::create();
    if (indices)
    {
        auto draw = vsg::VertexIndexDraw::create();
        draw->assignArrays({ vertices, colours });
        draw->assignIndices(indices);
        draw->indexCount = static_cast<uint32_t>(indices->size());
        draw->instanceCount = 1;
        state->addChild(draw);
    }
    else
    {
        auto draw = vsg::VertexDraw::create();
        draw->assignArrays({ vertices, colours });
        draw->vertexCount = static_cast<uint32_t>(vertices->size());
        draw->instanceCount = 1;
        state->addChild(draw);
    }
    return state;
}
