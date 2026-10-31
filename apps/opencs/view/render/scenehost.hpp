#ifndef CSV_RENDER_SCENEHOST_H
#define CSV_RENDER_SCENEHOST_H

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <vsg/app/ViewMatrix.h>
#include <vsg/core/Array.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/Switch.h>

#include <components/esm/refid.hpp>
#include <components/mwanimation/context.hpp>

#include "lighting.hpp"

namespace vsg
{
    class Camera;
    class ProjectionMatrix;
    class RenderGraph;
    class View;
    class ViewportState;
}

namespace vsgQt
{
    class Window;
}

namespace Render
{
    class Engine;
}

namespace Resource
{
    class ResourceSystem;
}

namespace View
{
    class LightGrid;
    struct Scene;
}

namespace CSMWorld
{
    class Data;
}

namespace CSVRender
{
    class RenderContext;

    /// Told once before each frame. Removes itself before it goes.
    class FrameListener
    {
    public:
        virtual ~FrameListener() = default;
        virtual void frameUpdate(float dt) = 0;
        /// Does this move by itself (particles, flickering lights), so that a visible view draws every tick?
        virtual bool isAnimated() const { return false; }
    };

    /// One 3D view: the scene graph, camera, per-view descriptor set and command graphs of one window, what
    /// MWRender::RenderManager is for the game's one view. Device, render pass, compile context and resource caches come
    /// from RenderContext and CSMWorld::Data. Owned by the view that shows it, and must not outlive the RenderContext.
    /// Every method runs on the main thread. What it shows comes from CSVRender::Cell or Object, which add to contentRoot.
    class SceneHost
    {
    public:
        /// Drawn last, after everything else: cell markers, which the OSG editor put in render bin 11.
        static constexpr int sBinOverlay = 2;
        /// Transparent objects, sorted back to front. The game's number (apps/openmw/mwrender/bin.hpp).
        static constexpr int sBinDepthSorted = 0;

        /// Throws if the window has no size yet (Render::Engine's constructor).
        SceneHost(RenderContext& context, vsgQt::Window& window, CSMWorld::Data& data);
        ~SceneHost();

        SceneHost(const SceneHost&) = delete;
        SceneHost& operator=(const SceneHost&) = delete;

        /// Finds a cell by its record ID ("#-2 -9" for an exterior) or by name, the first match in the record order.
        /// \return the record ID, or empty if there is none.
        static ESM::RefId findCell(const CSMWorld::Data& data, const std::string& nameOrId);

        /// Compile a node before adding it here (vsgUtil::CompileContext) and detach it when it goes.
        vsg::Group& contentRoot() { return *mContentRoot; }
        vsg::ref_ptr<vsgUtil::CompileContext> compileContext() const;
        const MWAnim::Context& animContext() const { return mAnimContext; }
        Resource::ResourceSystem& resources() { return *mResources; }
        CSMWorld::Data& data() { return mData; }

        /// A node that shows for this element of mask.hpp's list when setVisibilityMask includes it.
        static vsg::ref_ptr<vsg::Switch> maskNode(unsigned int mask, vsg::ref_ptr<vsg::Node> child);

        void setVisibilityMask(unsigned int mask);

        void setCamera(const vsg::dvec3& eye, const vsg::dvec3& center, const vsg::dvec3& up);
        /// As the camera controllers need it. Asks for a frame if the view changed.
        vsg::dmat4 getViewMatrix() const;
        void setViewMatrix(const vsg::dmat4& view);
        vsg::dvec3 getCameraEye() const;
        vsg::dvec3 getCameraCenter() const;
        /// The world's size of one pixel at a position, and the camera's axes in the world: what a screen-facing marker
        /// needs.
        double worldPerPixel(const vsg::dvec3& position) const;
        vsg::dvec3 cameraRight() const;
        vsg::dvec3 cameraUp() const;

        /// Points the camera at the middle of these positions from outside them. Empty: leaves it.
        void frame(const std::vector<vsg::dvec3>& positions);

        /// \param fieldOfView in degrees, used unless ortho. \param orthoSize the preference camera-ortho-size.
        void setProjection(double fieldOfView, bool ortho, int orthoSize);

        void setLighting(const LightingValues& values);
        /// \param top the "background" colour \param bottom the "gradient" colour, used if gradient
        void setBackground(const vsg::vec4& top, const vsg::vec4& bottom, bool gradient);
        /// \param apply the preference scene-day-night-switch-nodes
        void setNightDayMode(unsigned int index, bool apply);
        unsigned int getNightDayIndex() const { return mNightDayIndex; }
        bool getNightDayApply() const { return mNightDayApply; }

        /// The nearest thing under a point of the view, where (0, 0) is the view's top left corner and (1, 1) its bottom
        /// right, among the elements of mask.hpp's list in \p mask.
        /// \return the world position, if anything is there.
        std::optional<vsg::dvec3> pickNearest(double x, double y, unsigned int mask) const;

        /// A frame will be drawn on the next timer tick, as flagAsModified was with OSG's ON_DEMAND.
        void requestRedraw() { mRedraw = true; }

        void addListener(FrameListener& listener);
        void removeListener(FrameListener& listener);

        /// False once Qt has destroyed the window's surface (vsgQt::Window::cleanup): nothing can be drawn here any more.
        bool hasWindow() const;

        /// Writes the last frame drawn to a file. \return whether there was a frame to write: a zero-size window has none.
        bool capture(const std::filesystem::path& file);

    private:
        friend class RenderContext;

        void update();
        bool needsFrame();
        void endFrame();
        void updateExtent();
        void updateProjection();
        void createGradient();
        void updateGradient();

        RenderContext& mContext;
        vsgQt::Window& mWindow;
        CSMWorld::Data& mData;
        std::shared_ptr<Resource::ResourceSystem> mResources;
        // Destroyed after everything compiled through it. Must outlive the graphs that refer to its window.
        std::unique_ptr<Render::Engine> mEngine;
        MWAnim::Context mAnimContext;

        std::unique_ptr<View::Scene> mSceneData;
        std::unique_ptr<View::LightGrid> mLightGrid;
        vsg::ref_ptr<vsg::Group> mSceneRoot;
        vsg::ref_ptr<vsg::Group> mContentRoot;
        vsg::ref_ptr<vsg::LookAt> mViewMatrix;
        vsg::ref_ptr<vsg::ProjectionMatrix> mProjection;
        vsg::ref_ptr<vsg::Camera> mCamera;
        vsg::ref_ptr<vsg::View> mView;
        vsg::ref_ptr<vsg::ViewportState> mViewport;
        vsg::ref_ptr<vsg::RenderGraph> mRenderGraph;
        VkExtent2D mExtent{};

        double mFieldOfView = 90.0;
        bool mOrtho = false;
        int mOrthoSize = 0;

        LightingValues mLighting;
        unsigned int mNightDayIndex = 0;
        bool mNightDayApply = false;

        vsg::vec4 mTopColour{};
        vsg::vec4 mBottomColour{};
        vsg::ref_ptr<vsg::Switch> mGradientSwitch;
        vsg::ref_ptr<vsg::vec3Array> mGradientVertices;
        vsg::ref_ptr<vsg::vec4Array> mGradientColours;

        bool mRedraw = true;
        std::vector<FrameListener*> mListeners;
        std::chrono::steady_clock::time_point mLastUpdate;
    };
}

#endif
