#include "scenecensus.hpp"

#include <components/vsgutil/distancelod.hpp>

#include <vsg/commands/Draw.h>
#include <vsg/commands/DrawIndexed.h>
#include <vsg/nodes/Geometry.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/Transform.h>
#include <vsg/nodes/VertexDraw.h>
#include <vsg/nodes/VertexIndexDraw.h>

namespace Render
{
    void SceneCensus::apply(const vsg::Node& node)
    {
        node.traverse(*this);
    }

    void SceneCensus::apply(const vsg::Transform& transform)
    {
        if (!mHasEye)
        {
            transform.traverse(*this);
            return;
        }
        const vsg::dmat4 parent = mMatrices.empty() ? vsg::dmat4() : mMatrices.back();
        mMatrices.push_back(transform.transform(parent));
        transform.traverse(*this);
        mMatrices.pop_back();
    }

    void SceneCensus::apply(const vsg::Group& group)
    {
        const auto* lod = mHasEye ? dynamic_cast<const vsgUtil::DistanceLOD*>(&group) : nullptr;
        if (!lod)
        {
            group.traverse(*this);
            return;
        }
        // As DistanceLOD::accept, with the eye in world space: the distance is taken in the node's own units.
        const vsg::dmat4 model = mMatrices.empty() ? vsg::dmat4() : mMatrices.back();
        const vsg::dvec4 centre = model * vsg::dvec4(lod->center.x, lod->center.y, lod->center.z, 1.0);
        const double scale = vsg::length(vsg::dvec3(model[0][0], model[0][1], model[0][2]));
        const double distance
            = vsg::length(vsg::dvec3(centre.x, centre.y, centre.z) - mEye) / (scale > 0.0 ? scale : 1.0);
        ++mLodNodes;
        for (std::size_t i = 0; i < lod->children.size(); ++i)
        {
            if (lod->isVisible(i, distance))
                lod->children[i]->accept(*this);
            else
                ++mLodChildrenSkipped;
        }
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
