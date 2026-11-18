#ifndef OPENEDEN_RENDER_SCENECENSUS_H
#define OPENEDEN_RENDER_SCENECENSUS_H

#include <cstdint>

#include <vector>

#include <vsg/core/ConstVisitor.h>
#include <vsg/maths/mat4.h>
#include <vsg/maths/vec3.h>

namespace Render
{
    /*
     * Task 0054 (S0). Counts the draw commands and triangles reachable in a scene graph. It does no frustum culling
     * and does not know about level of detail choices, so the totals are an upper bound of what a frame submits (the
     * engine has no way to count the commands it really issues: VSG's record traversal cannot be hooked). A switch's
     * children that are switched off are not counted (Switch::traverse honours the masks). A node reached twice is
     * counted twice, as it would be drawn twice. Draw nodes of types it does not know (instanced and indirect draws)
     * are not counted.
     * Triangles are index count (or vertex count) / 3 * instance count.
     * Task 0062 stage a: with an eye set (`setEye`), a vsgUtil::DistanceLOD counts only the children whose range holds
     * the distance from the eye (as its record traversal does), which needs the transforms above it (MatrixTransform
     * and Transform are tracked). Without an eye every child is counted, as before. `lodNodes` and `lodChildrenSkipped`
     * say how much the ranges removed.
     * Not thread safe: use it on the main thread, when the scene is not being edited.
     */
    class SceneCensus : public vsg::ConstVisitor
    {
    public:
        void reset()
        {
            mDraws = 0;
            mTriangles = 0;
            mLodNodes = 0;
            mLodChildrenSkipped = 0;
            mMatrices.clear();
        }
        /// Apply DistanceLOD ranges for this eye (world space). Cleared by `clearEye`.
        void setEye(const vsg::dvec3& eye)
        {
            mEye = eye;
            mHasEye = true;
        }
        void clearEye() { mHasEye = false; }
        std::uint64_t lodNodes() const { return mLodNodes; }
        std::uint64_t lodChildrenSkipped() const { return mLodChildrenSkipped; }
        std::uint64_t draws() const { return mDraws; }
        std::uint64_t triangles() const { return mTriangles; }

        using vsg::ConstVisitor::apply;
        // ConstVisitor does not walk into children by itself; every node type without an override above ends up here.
        void apply(const vsg::Node& node) override;
        void apply(const vsg::Group& group) override;
        void apply(const vsg::Transform& transform) override;
        void apply(const vsg::VertexIndexDraw& draw) override;
        void apply(const vsg::VertexDraw& draw) override;
        void apply(const vsg::Geometry& geometry) override;
        void apply(const vsg::Draw& draw) override;
        void apply(const vsg::DrawIndexed& draw) override;

    private:
        void count(std::uint32_t elements, std::uint32_t instances);

        std::uint64_t mDraws = 0;
        std::uint64_t mTriangles = 0;
        std::uint64_t mLodNodes = 0;
        std::uint64_t mLodChildrenSkipped = 0;
        bool mHasEye = false;
        vsg::dvec3 mEye;
        std::vector<vsg::dmat4> mMatrices; // model matrices above the node being visited (empty: identity)
    };
}

#endif
