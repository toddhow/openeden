#include "physicshub.hpp"

#include <algorithm>
#include <optional>

#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

#include <components/debug/debuglog.hpp>
#include <components/render/debuggeometry.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/setbin.hpp>

#include "scenehost.hpp"

namespace CSVRender
{
    namespace
    {
        // Render::DebugGeometry's capacity, in vertices (two per line). A ball draws a few thousand lines; lines past
        // this are dropped, and the renderer says so once.
        constexpr std::size_t cLineVertexCapacity = 65536;
        // The module overlay's filled triangles (API 1.1): three vertices each, as many as the overlay may hold.
        constexpr std::size_t cFillVertexCapacity = 3 * CSMModule::kMaxOverlayTriangles;
        // The test fixture of PhysicsHub::dropTestBall, in meters.
        constexpr float cSlabHalfSize = 20.0f;
        constexpr float cSlabHalfThickness = 0.5f;
        constexpr float cBallRadius = 3.0f;
        constexpr float cBallHeight = 12.0f;
    }

    PhysicsHub* PhysicsHub::sInstance = nullptr;

    PhysicsHub::PhysicsHub(CSMPhysics::StudioWorld& world)
        : mWorld(world)
    {
        sInstance = this;
        connect(&mTimer, &QTimer::timeout, this, &PhysicsHub::tick);
        mTimer.start(sTickMilliseconds);
    }

    PhysicsHub::~PhysicsHub()
    {
        mTimer.stop();
        sInstance = nullptr;
    }

    void PhysicsHub::play()
    {
        mWorld.play();
        emit stateChanged();
    }

    void PhysicsHub::pause()
    {
        mWorld.pause();
        emit stateChanged();
    }

    void PhysicsHub::step()
    {
        mWorld.stepOnce();
        refresh();
        emit stateChanged();
    }

    void PhysicsHub::dropTestBall()
    {
        constexpr float units = CSMPhysics::StudioWorld::sUnitsPerMeter;
        vsg::dvec3 centre(0.0, 0.0, 0.0);
        if (!mViews.empty())
        {
            // What is under the middle of the first view (terrain or an object), else the camera's centre.
            SceneHost& host = mViews.front()->host();
            const std::optional<vsg::dvec3> hit = host.pickNearest(0.5, 0.5, ~0u);
            centre = hit ? *hit : host.getCameraCenter();
        }

        Log(Debug::Info) << "Physics test ball above " << centre.x << ", " << centre.y << ", " << centre.z;
        try
        {
            if (mWorld.bodyCount() == 0)
            {
                // The slab's top face is at the picked height.
                const float half = cSlabHalfSize * units;
                const float halfZ = cSlabHalfThickness * units;
                mWorld.addStaticBox(JPH::RVec3(centre.x, centre.y, centre.z - halfZ), JPH::Vec3(half, half, halfZ));
            }
            mLastBall = mWorld.addDynamicSphere(
                JPH::RVec3(centre.x, centre.y, centre.z + (cBallHeight + cBallRadius) * units), cBallRadius * units);
        }
        catch (const std::exception& e)
        {
            // A slot must not throw: a full world is reported and left as it is.
            Log(Debug::Error) << "Cannot add the test ball: " << e.what();
        }
        refresh();
        emit stateChanged();
    }

    void PhysicsHub::clearWorld()
    {
        mWorld.clear();
        mLastBall = JPH::BodyID();
        refresh();
        emit stateChanged();
    }

    void PhysicsHub::setDebugDraw(bool enabled)
    {
        mDebugDraw = enabled;
        refresh();
        emit stateChanged();
    }

    void PhysicsHub::add(PhysicsDebugView& view)
    {
        mViews.push_back(&view);
        // Make the new view show what the others do, at the next refresh.
        mDrawnRevision = ~std::uint64_t(0);
    }

    void PhysicsHub::remove(PhysicsDebugView& view)
    {
        mViews.erase(std::remove(mViews.begin(), mViews.end(), &view), mViews.end());
    }

    void PhysicsHub::tick()
    {
        const bool stepped = mWorld.tick();
        if (stepped)
            refresh();
        if (isPlaying() != mWasPlaying)
        {
            mWasPlaying = isPlaying();
            emit stateChanged();
        }
        else if (!stepped)
            refresh();
    }

    void PhysicsHub::refresh()
    {
        const bool wantDraw = mDebugDraw && mWorld.bodyCount() > 0;
        if (mWorld.revision() == mDrawnRevision && wantDraw == mDrawnEnabled)
            return;
        mDrawnRevision = mWorld.revision();
        mDrawnEnabled = wantDraw;
        if (wantDraw)
            mDrawer.rebuild(mWorld.system());
        const std::vector<CSMPhysics::DebugLine> none;
        for (PhysicsDebugView* view : mViews)
            view->show(wantDraw ? mDrawer.lines() : none);
    }

    PhysicsDebugView::PhysicsDebugView(SceneHost& host, bool registerWithHub)
        : mHost(host)
        , mHostAlive(host.aliveToken())
        , mRegistered(registerWithHub)
        , mGeometry(std::make_unique<Render::DebugGeometry>(
              *host.resources().builder, host.compileContext(), cLineVertexCapacity, 0))
    {
        // In the overlay bin, drawn after everything else as the editor's other helpers are.
        mRoot = vsgUtil::createBinSetter(SceneHost::sBinOverlay, mGeometry->node());
        host.contentRoot().addChild(mRoot);
        if (PhysicsHub* hub = PhysicsHub::get(); hub != nullptr && registerWithHub)
            hub->add(*this);
    }

    void PhysicsDebugView::removeFromContent(const vsg::ref_ptr<vsg::Node>& root)
    {
        if (!root || mHostAlive.expired())
            return;
        auto& children = mHost.contentRoot().children;
        children.erase(std::remove_if(children.begin(), children.end(),
                           [&root](const vsg::ref_ptr<vsg::Node>& child) { return child.get() == root.get(); }),
            children.end());
    }

    PhysicsDebugView::~PhysicsDebugView()
    {
        if (PhysicsHub* hub = PhysicsHub::get(); hub != nullptr && mRegistered)
            hub->remove(*this);
        removeFromContent(mRoot);
        removeFromContent(mFill.mRoot);
        removeFromContent(mFillOnTop.mRoot);
    }

    void PhysicsDebugView::show(const std::vector<CSMPhysics::DebugLine>& lines)
    {
        if (mHostAlive.expired())
            return;
        if (lines.empty())
        {
            if (mGeometry->isEnabled())
            {
                mGeometry->setEnabled(false);
                mHost.requestRedraw();
            }
            return;
        }
        mGeometry->setEnabled(true);
        mGeometry->begin();
        for (const CSMPhysics::DebugLine& line : lines)
            mGeometry->addLine(vsg::vec3(line.mFrom[0], line.mFrom[1], line.mFrom[2]),
                vsg::vec3(line.mTo[0], line.mTo[1], line.mTo[2]),
                vsg::vec4(line.mColour[0], line.mColour[1], line.mColour[2], line.mColour[3]));
        mGeometry->end();
        mHost.requestRedraw();
    }

    void PhysicsDebugView::showFill(
        Fill& fill, bool alwaysOnTop, const std::vector<const CSMModule::OverlayTriangle*>& triangles)
    {
        if (triangles.empty())
        {
            if (fill.mGeometry && fill.mGeometry->isEnabled())
                fill.mGeometry->setEnabled(false);
            return;
        }
        if (!fill.mGeometry)
        {
            fill.mGeometry = std::make_unique<Render::DebugGeometry>(
                *mHost.resources().builder, mHost.compileContext(), 0, cFillVertexCapacity, !alwaysOnTop);
            fill.mRoot = vsgUtil::createBinSetter(SceneHost::sBinOverlay, fill.mGeometry->node());
            // Before the lines' node, so that the edges draw over the fill (the overlay bin does not sort).
            auto& children = mHost.contentRoot().children;
            const auto lines = std::find_if(children.begin(), children.end(),
                [this](const vsg::ref_ptr<vsg::Node>& child) { return child.get() == mRoot.get(); });
            children.insert(lines, fill.mRoot);
        }
        fill.mGeometry->setEnabled(true);
        fill.mGeometry->begin();
        for (const CSMModule::OverlayTriangle* triangle : triangles)
            fill.mGeometry->addTriangle(
                vsg::vec3(triangle->mVertices[0][0], triangle->mVertices[0][1], triangle->mVertices[0][2]),
                vsg::vec3(triangle->mVertices[1][0], triangle->mVertices[1][1], triangle->mVertices[1][2]),
                vsg::vec3(triangle->mVertices[2][0], triangle->mVertices[2][1], triangle->mVertices[2][2]),
                vsg::vec4(triangle->mColour[0], triangle->mColour[1], triangle->mColour[2], triangle->mColour[3]));
        fill.mGeometry->end();
    }

    void PhysicsDebugView::show(const CSMModule::OverlayData& overlay)
    {
        if (mHostAlive.expired())
            return;
        std::vector<const CSMModule::OverlayTriangle*> tested;
        std::vector<const CSMModule::OverlayTriangle*> onTop;
        tested.reserve(overlay.mTriangles.size());
        for (const CSMModule::OverlayTriangle& triangle : overlay.mTriangles)
            (triangle.mAlwaysOnTop ? onTop : tested).push_back(&triangle);
        showFill(mFill, false, tested);
        showFill(mFillOnTop, true, onTop);
        show(overlay.mLines);
        mHost.requestRedraw();
    }
}
