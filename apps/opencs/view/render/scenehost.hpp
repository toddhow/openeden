#ifndef CSV_RENDER_SCENEHOST_H
#define CSV_RENDER_SCENEHOST_H

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <vsg/app/ViewMatrix.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>
#include <vsg/nodes/Group.h>

#include <components/esm/refid.hpp>
#include <components/mwanimation/context.hpp>

namespace vsg
{
    class Camera;
    class Perspective;
    class RenderGraph;
    class ViewportState;
}

namespace vsgQt
{
    class Window;
}

namespace MWAnim
{
    class Object;
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

    /// One 3D view: the scene graph, camera, per-view descriptor set and command graphs of one window, what
    /// MWRender::RenderManager is for the game's one view. Device, render pass, compile context and resource caches come
    /// from RenderContext and CSMWorld::Data. Owned by the view that shows it, and must not outlive the RenderContext.
    /// Every method runs on the main thread.
    ///
    /// Stage C2 draws a cell's objects with their lights. No actors, terrain, water, cell markers or lighting modes: those
    /// are stages C3 and C4.
    class SceneHost
    {
    public:
        /// Throws if the window has no size yet (Render::Engine's constructor).
        SceneHost(RenderContext& context, vsgQt::Window& window, CSMWorld::Data& data);
        ~SceneHost();

        SceneHost(const SceneHost&) = delete;
        SceneHost& operator=(const SceneHost&) = delete;

        /// Finds a cell by its record ID ("#-2 -9" for an exterior) or by name, the first match in the record order.
        /// \return the record ID, or empty if there is none.
        static ESM::RefId findCell(const CSMWorld::Data& data, const std::string& nameOrId);

        /// Replaces what is shown by the objects of one cell, and points the camera at them.
        /// \return the number of objects shown.
        std::size_t showCell(const ESM::RefId& cellId);

        void setCamera(const vsg::dvec3& eye, const vsg::dvec3& center, const vsg::dvec3& up);

        /// Writes the last frame drawn to a file. \return whether there was a frame to write: a zero-size window has none.
        bool capture(const std::filesystem::path& file);

    private:
        friend class RenderContext;

        void update();
        void endFrame();
        void updateExtent();

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
        vsg::ref_ptr<vsg::Group> mObjectRoot;
        vsg::ref_ptr<vsg::LookAt> mViewMatrix;
        vsg::ref_ptr<vsg::Perspective> mProjection;
        vsg::ref_ptr<vsg::ViewportState> mViewport;
        vsg::ref_ptr<vsg::RenderGraph> mRenderGraph;
        VkExtent2D mExtent{};

        vsg::vec4 mAmbient{};
        vsg::vec4 mLightDiffuse{};
        vsg::vec4 mLightSpecular{};
        vsg::vec3 mLightPosition{};

        std::vector<std::unique_ptr<MWAnim::Object>> mObjects;
        std::chrono::steady_clock::time_point mLastUpdate;
    };
}

#endif
