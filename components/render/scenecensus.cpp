#include "scenecensus.hpp"

#include <vsg/commands/Draw.h>
#include <vsg/commands/DrawIndexed.h>
#include <vsg/nodes/Geometry.h>
#include <vsg/nodes/VertexDraw.h>
#include <vsg/nodes/VertexIndexDraw.h>

namespace Render
{
    void SceneCensus::apply(const vsg::Node& node)
    {
        node.traverse(*this);
    }

    void SceneCensus::count(std::uint32_t elements, std::uint32_t instances)
    {
        ++mDraws;
        mTriangles += static_cast<std::uint64_t>(elements / 3) * instances;
    }

    void SceneCensus::apply(const vsg::VertexIndexDraw& draw)
    {
        count(draw.indexCount, draw.instanceCount);
    }

    void SceneCensus::apply(const vsg::VertexDraw& draw)
    {
        count(draw.vertexCount, draw.instanceCount);
    }

    void SceneCensus::apply(const vsg::Draw& draw)
    {
        count(draw.vertexCount, draw.instanceCount);
    }

    void SceneCensus::apply(const vsg::DrawIndexed& draw)
    {
        count(draw.indexCount, draw.instanceCount);
    }

    void SceneCensus::apply(const vsg::Geometry& geometry)
    {
        // A Geometry does not traverse its draw commands, so they are visited here.
        for (const auto& command : geometry.commands)
            if (command)
                command->accept(*this);
    }
}
