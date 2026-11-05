#include "meshviewwidget.hpp"

#include <QKeyEvent>
#include <QMouseEvent>

#include <components/debug/debuglog.hpp>

#include "../module/moduleui.hpp"
#include "cameracontroller.hpp"
#include "lighting.hpp"
#include "physicshub.hpp"
#include "picking.hpp"
#include <apps/opencs/model/mesh/framemath.hpp>

namespace CSVRender
{
    namespace
    {
        // The viewer's own field of view, in degrees: a model fills the view without a wide angle's stretching.
        constexpr double sFieldOfView = 50.0;
        // A click that moved further than this many pixels was an orbit or a pan.
        constexpr int sClickTolerance = 4;
        // The way the camera looks at a new model: along +Y and down, so that it sits in front, above and to the left.
        const std::array<double, 3> sViewDirection{ 0.5, 1.0, -0.4 };
    }

    MeshViewWidget::MeshViewWidget(CSMWorld::Data& worldData, QWidget* parent)
        : SceneWidget(worldData, parent)
    {
        selectNavigationMode("orbit");
        setExterior(false);
        setFocusPolicy(Qt::StrongFocus);
    }

    MeshViewWidget::~MeshViewWidget()
    {
        // The scene and the overlay detach their nodes from the host, which the base class destroys after this body.
        mScene.reset();
        mModuleOverlay.reset();
    }

    void MeshViewWidget::openMesh(const std::string& path)
    {
        if (path.empty())
        {
            mModuleAsset = true;
            if (getHost())
                showModuleAsset();
            return;
        }
        mPendingPath = path;
        if (getHost())
            load();
    }

    void MeshViewWidget::hostReady()
    {
        SceneWidget::hostReady();
        getHost()->setProjection(sFieldOfView, false, 0);
        applyViewLighting();
        if (mModuleAsset)
            showModuleAsset();
        else if (!mPendingPath.empty())
            load();
    }

    void MeshViewWidget::showModuleAsset()
    {
        CSVModule::ModuleUi* modules = CSVModule::ModuleUi::get();
        if (modules == nullptr || mModuleOverlay)
            return;
        // Filled with show(), not by the physics hub.
        mModuleOverlay = std::make_unique<PhysicsDebugView>(*getHost(), false);
        connect(modules, &CSVModule::ModuleUi::changed, this, [this] { updateModuleOverlay(); });
        updateModuleOverlay();
    }

    void MeshViewWidget::updateModuleOverlay()
    {
        CSVModule::ModuleUi* modules = CSVModule::ModuleUi::get();
        if (modules == nullptr || !mModuleOverlay || getHost() == nullptr)
            return;
        // Module coordinates are metres relative to the origin given here: the view's own origin.
        const float origin[3] = { 0.f, 0.f, 0.f };
        const CSMModule::OverlayData overlay = modules->overlay(origin);
        const std::vector<CSMPhysics::DebugLine>& lines = overlay.mLines;
        mModuleOverlay->show(overlay);
        mModuleLines = lines.size();
        mModuleBounds = CSMMesh::Aabb();
        for (const CSMPhysics::DebugLine& line : lines)
        {
            CSMMesh::expand(mModuleBounds, { line.mFrom[0], line.mFrom[1], line.mFrom[2] });
            CSMMesh::expand(mModuleBounds, { line.mTo[0], line.mTo[1], line.mTo[2] });
        }
        for (const CSMModule::OverlayTriangle& triangle : overlay.mTriangles)
            for (const float(&vertex)[3] : triangle.mVertices)
                CSMMesh::expand(mModuleBounds, { vertex[0], vertex[1], vertex[2] });
        // A new asset is framed once; an edit of the same asset keeps the camera.
        if (mModuleGeneration != modules->generation() && (!lines.empty() || !overlay.mTriangles.empty()))
        {
            mModuleGeneration = modules->generation();
            frameObject();
        }
        flagAsModified();
        emit moduleOverlayChanged(static_cast<int>(lines.size()));
    }

    void MeshViewWidget::load()
    {
        const std::string path = std::move(mPendingPath);
        mPendingPath.clear();
        mScene.reset();
        auto scene = std::make_unique<MeshScene>(*getHost());
        std::string error;
        if (!scene->load(path, error))
        {
            Log(Debug::Error) << "Mesh viewer: " << error;
            emit loadFailed(QString::fromStdString(error));
            return;
        }
        mScene = std::move(scene);
        mScene->setState(mState);
        frameObject();
        emit modelLoaded();
        flagAsModified();
    }

    void MeshViewWidget::setViewState(const CSMMesh::MeshViewState& state)
    {
        mState = state;
        if (mScene)
            mScene->setState(mState);
        applyViewLighting();
        flagAsModified();
    }

    void MeshViewWidget::applyViewLighting()
    {
        SceneHost* host = getHost();
        if (!host)
            return;
        const vsg::vec4 white(1.f, 1.f, 1.f, 1.f);
        LightingValues values{ vsg::vec4(0.45f, 0.45f, 0.45f, 1.f), white, 0 };
        switch (mState.lighting)
        {
            case CSMMesh::LightingPreset::Headlight:
                break;
            case CSMMesh::LightingPreset::Sun:
                values = lightingValues(LightingMode::Day, std::nullopt, false);
                break;
            case CSMMesh::LightingPreset::Flat:
                values = { white, vsg::vec4(0.f, 0.f, 0.f, 1.f), 0 };
                break;
        }
        host->setLighting(values);
        const vsg::vec4 background(mState.background[0], mState.background[1], mState.background[2], 1.f);
        host->setBackground(background, background, false);
    }

    void MeshViewWidget::frameObject()
    {
        if ((!mScene && !mModuleAsset) || !getHost())
            return;
        const double aspect = height() > 0 ? static_cast<double>(width()) / height() : 1.0;
        const CSMMesh::FramePose pose
            = CSMMesh::frameCamera(mScene ? mScene->bounds() : mModuleBounds, sFieldOfView, aspect, sViewDirection);
        const vsg::dvec3 centre(pose.centre[0], pose.centre[1], pose.centre[2]);
        setCameraPose(
            vsg::dvec3(pose.eye[0], pose.eye[1], pose.eye[2]), centre, vsg::dvec3(pose.up[0], pose.up[1], pose.up[2]));
        if (mCurrentCamControl == mOrbitCamControl)
            mOrbitCamControl->setCenter(centre);
        flagAsModified();
    }

    void MeshViewWidget::selectNode(CSMMesh::NodeId node)
    {
        if (mScene)
            mScene->setSelected(node);
    }

    bool MeshViewWidget::saveScreenshot(const std::filesystem::path& file)
    {
        return getHost() && getHost()->capture(file);
    }

    void MeshViewWidget::frameUpdate(float dt)
    {
        SceneWidget::frameUpdate(dt);
        if (mScene)
            mScene->update(dt);
    }

    bool MeshViewWidget::isAnimated() const
    {
        return mScene && mScene->isAnimated();
    }

    void MeshViewWidget::mousePressEvent(QMouseEvent* event)
    {
        mPressPosition = event->pos();
        SceneWidget::mousePressEvent(event);
    }

    void MeshViewWidget::mouseReleaseEvent(QMouseEvent* event)
    {
        SceneWidget::mouseReleaseEvent(event);
        if (event->button() != Qt::LeftButton || !mScene || !getHost() || width() <= 0 || height() <= 0)
            return;
        if ((event->pos() - mPressPosition).manhattanLength() > sClickTolerance)
            return;
        emit nodePicked(
            pickAt(static_cast<double>(event->pos().x()) / width(), static_cast<double>(event->pos().y()) / height()));
    }

    CSMMesh::NodeId MeshViewWidget::pickAt(double x, double y) const
    {
        if (!mScene || !getHost())
            return CSMMesh::kNoNode;
        // From the collected triangles, so it works in every view mode.
        const Picking::Ray ray = Picking::viewRay(getHost()->getProjectionMatrix(), getHost()->getViewMatrix(), x, y);
        return mScene->pickNode(ray.mStart, ray.mEnd);
    }

    void MeshViewWidget::keyPressEvent(QKeyEvent* event)
    {
        if (event->key() == Qt::Key_F && event->modifiers() == Qt::NoModifier)
        {
            frameObject();
            return;
        }
        SceneWidget::keyPressEvent(event);
    }
}
