#ifndef CSV_RENDER_OVERLAYLINES_H
#define CSV_RENDER_OVERLAYLINES_H

#include <memory>
#include <vector>

#include <vsg/core/Array.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>
#include <vsg/nodes/Group.h>

namespace vsg
{
    class VertexDraw;
}

namespace CSVRender
{
    class SceneHost;

    /// Coloured lines drawn over everything, unlit and not pickable, that change often: what the OSG editor put in render
    /// bin 11 with no depth test. As Render::DebugGeometry, the arrays are kept and refilled, so that a change needs no
    /// compile unless the lines outgrow them. Owned by whichever tool draws them, which may outlive the SceneHost.
    class OverlayLines
    {
    public:
        explicit OverlayLines(SceneHost& host);
        ~OverlayLines();

        OverlayLines(const OverlayLines&) = delete;
        OverlayLines& operator=(const OverlayLines&) = delete;

        /// A list of lines, two vertices each. Replaces what was there.
        void set(const std::vector<vsg::vec3>& vertices, const vsg::vec4& colour);
        void set(const std::vector<vsg::vec3>& vertices, const std::vector<vsg::vec4>& colours);
        void clear();

    private:
        void reserve(std::size_t count);

        SceneHost& mHost;
        std::weak_ptr<void> mHostAlive;
        vsg::ref_ptr<vsg::Group> mRoot;
        vsg::ref_ptr<vsg::Node> mNode;
        vsg::ref_ptr<vsg::VertexDraw> mDraw;
        vsg::ref_ptr<vsg::vec3Array> mVertices;
        vsg::ref_ptr<vsg::vec4Array> mColours;
    };
}

#endif
