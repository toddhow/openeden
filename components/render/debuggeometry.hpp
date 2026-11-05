#ifndef OPENEDEN_RENDER_DEBUGGEOMETRY_H
#define OPENEDEN_RENDER_DEBUGGEOMETRY_H

#include <cstddef>
#include <mutex>

#include <vsg/core/Array.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>
#include <vsg/nodes/Group.h>

#include <components/sceneutil/detourdebugdraw.hpp>

namespace Pipeline
{
    class Builder;
}

namespace vsgUtil
{
    class CompileContext;
}

namespace vsg
{
    class VertexDraw;
}

namespace Render
{
    /**
     * @brief Debug geometry submitted as data and drawn by the renderer (task 0011): coloured lines and translucent
     * triangles. A producer such as the Jolt debug drawer or the navmesh renderer fills it between begin() and end();
     * the contents stay until the next begin(), so a producer that rarely changes need not refill every frame.
     *
     * Owned by the renderer (the game's RenderManager), whose scene graph holds node(). Vertex storage is
     * created the first time the geometry is enabled and has a fixed capacity per primitive type, so filling allocates
     * nothing; primitives past the capacity are dropped (reported once). Colours are drawn unlit, with alpha blending.
     * addLine and addTriangle may be called from several threads between begin() and end(); begin(), end() and
     * setEnabled() run on the main thread before the frame is recorded.
     */
    class DebugGeometry final : public SceneUtil::DebugPrimitiveSink
    {
    public:
        /// Capacities are in vertices (two per line, three per triangle); each vertex takes 28 bytes while enabled.
        /// \param depthTest false draws over everything (Eden Studio's "always on top" overlay shapes); the game's
        /// geometry always tests.
        DebugGeometry(const Pipeline::Builder& builder, vsg::ref_ptr<vsgUtil::CompileContext> compile,
            std::size_t lineVertexCapacity, std::size_t triangleVertexCapacity, bool depthTest = true);
        ~DebugGeometry() override;

        vsg::ref_ptr<vsg::Group> node() const { return mRoot; }

        void setEnabled(bool enabled);
        bool isEnabled() const { return mEnabled; }

        void begin();
        void addLine(const vsg::vec3& from, const vsg::vec3& to, const vsg::vec4& color) override;
        void addTriangle(const vsg::vec3& a, const vsg::vec3& b, const vsg::vec3& c, const vsg::vec4& color) override;
        void end();

    private:
        struct Batch
        {
            std::size_t mCapacity = 0;
            vsg::ref_ptr<vsg::VertexDraw> mDraw;
            vsg::ref_ptr<vsg::vec3Array> mVertices;
            vsg::ref_ptr<vsg::vec4Array> mColors;
            std::size_t mCount = 0;
            bool mReportedFull = false;
        };

        void create();
        vsg::ref_ptr<vsg::Node> createBatch(Batch& batch, VkPrimitiveTopology topology);
        bool reserve(Batch& batch, std::size_t vertices, const char* name);

        const Pipeline::Builder& mBuilder;
        vsg::ref_ptr<vsgUtil::CompileContext> mCompile;
        vsg::ref_ptr<vsg::Group> mRoot;
        vsg::ref_ptr<vsg::Group> mContents;
        Batch mLines;
        Batch mTriangles;
        std::mutex mMutex;
        bool mEnabled = false;
        bool mDepthTest = true;
    };
}

#endif
