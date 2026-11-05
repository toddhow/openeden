#ifndef CSV_RENDER_PHYSICSHUB_H
#define CSV_RENDER_PHYSICSHUB_H

#include <cstdint>
#include <memory>
#include <vector>

#include <QObject>
#include <QTimer>

#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Node.h>

#include <apps/opencs/model/module/overlaybuffer.hpp>
#include <apps/opencs/model/physics/debuglinedrawer.hpp>
#include <apps/opencs/model/physics/studioworld.hpp>

namespace Render
{
    class DebugGeometry;
}

namespace CSVRender
{
    class PhysicsDebugView;
    class SceneHost;

    /// What ties Eden Studio's one StudioWorld to the screen (task 0030): the timer that steps it while it plays, the
    /// one debug drawer (JPH::DebugRenderer is a process singleton) and the list of 3D views that show its bodies. The
    /// transport buttons and the Simulation menu talk to it. One exists while the editor runs (get() is null in tools
    /// that have no editor, such as the capture mode).
    ///
    /// Owned by CS::Editor, which also owns the world; destroyed after every view. Main thread only.
    class PhysicsHub : public QObject
    {
        Q_OBJECT

    public:
        /// How often the playing world is stepped. The step itself is fixed (StudioWorld::sStepSeconds): a tick is one
        /// step, so a slow machine plays in slow motion rather than taking bigger steps.
        static constexpr int sTickMilliseconds = 16;

        /// \param world must outlive the hub.
        explicit PhysicsHub(CSMPhysics::StudioWorld& world);
        ~PhysicsHub() override;

        PhysicsHub(const PhysicsHub&) = delete;
        PhysicsHub& operator=(const PhysicsHub&) = delete;

        /// The editor's hub, or null.
        static PhysicsHub* get() { return sInstance; }

        CSMPhysics::StudioWorld& world() { return mWorld; }

        void play();
        void pause();
        void step();
        /// Test fixture until cells fill the world: a ground slab under the middle of the first 3D view's camera and a
        /// ball (3 m radius, so that it can be seen from a cell-sized distance) dropped 12 m above it. The slab's
        /// middle is what is under the middle of the view, else the camera's centre.
        void dropTestBall();
        void clearWorld();

        /// The ball dropTestBall added last (invalid before the first).
        JPH::BodyID lastBall() const { return mLastBall; }

        bool isPlaying() const { return mWorld.transport() == CSMPhysics::StudioWorld::Transport::Playing; }

        /// The "Physics Debug Draw" switch.
        bool debugDrawEnabled() const { return mDebugDraw; }
        void setDebugDraw(bool enabled);

        /// Called by PhysicsDebugView.
        void add(PhysicsDebugView& view);
        void remove(PhysicsDebugView& view);

        /// Draws the world into every view now, if anything changed since the last time. Called by the timer.
        void refresh();

    signals:
        /// The transport state, the body count or the draw switch changed.
        void stateChanged();

    private:
        void tick();

        static PhysicsHub* sInstance;

        CSMPhysics::StudioWorld& mWorld;
        // After mWorld: the drawer reads it. The only JPH::DebugRenderer of the process.
        CSMPhysics::DebugLineDrawer mDrawer;
        QTimer mTimer;
        std::vector<PhysicsDebugView*> mViews;
        bool mDebugDraw = true;
        std::uint64_t mDrawnRevision = ~std::uint64_t(0);
        bool mDrawnEnabled = false;
        bool mWasPlaying = false;
        JPH::BodyID mLastBall;
    };

    /// One 3D view's copy of the world's debug lines: a Render::DebugGeometry under the SceneHost's contentRoot. Owned
    /// by the world-space widget that shows the view, and removed with it.
    class PhysicsDebugView
    {
    public:
        /// \param registerWithHub the view shows the physics world (true), or is filled by its owner with show().
        explicit PhysicsDebugView(SceneHost& host, bool registerWithHub = true);
        ~PhysicsDebugView();

        PhysicsDebugView(const PhysicsDebugView&) = delete;
        PhysicsDebugView& operator=(const PhysicsDebugView&) = delete;

        SceneHost& host() { return mHost; }

        /// Replaces what is drawn. An empty list switches the geometry off.
        void show(const std::vector<CSMPhysics::DebugLine>& lines);

        /// Replaces what is drawn with the modules' overlay (Editor Module API 1.1): the lines, and the filled
        /// triangles under them. Nothing at all switches everything off. The triangle geometry is made the first time
        /// a triangle comes, so a view that only ever shows lines never pays for it.
        void show(const CSMModule::OverlayData& overlay);

    private:
        /// One set of filled triangles (depth tested or always on top) and its node under contentRoot.
        struct Fill
        {
            std::unique_ptr<Render::DebugGeometry> mGeometry;
            vsg::ref_ptr<vsg::Node> mRoot;
        };

        void showFill(Fill& fill, bool alwaysOnTop, const std::vector<const CSMModule::OverlayTriangle*>& triangles);
        void removeFromContent(const vsg::ref_ptr<vsg::Node>& root);

        SceneHost& mHost;
        std::weak_ptr<void> mHostAlive;
        bool mRegistered;
        std::unique_ptr<Render::DebugGeometry> mGeometry;
        vsg::ref_ptr<vsg::Node> mRoot; ///< The geometry's node in the overlay bin: what hangs under contentRoot.
        Fill mFill; ///< Depth tested triangles; before mRoot under contentRoot, so that the lines draw over them.
        Fill mFillOnTop; ///< Triangles drawn over everything.
    };
}

#endif
