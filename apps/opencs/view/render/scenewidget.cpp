#include "scenewidget.hpp"

#include <exception>

#include <QCoreApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QTimer>
#include <QWheelEvent>

#include <vsg/ui/PointerEvent.h>
#include <vsgQt/Window.h>

#include <apps/opencs/model/prefs/category.hpp>
#include <apps/opencs/model/prefs/setting.hpp>
#include <apps/opencs/model/world/data.hpp>

#include <components/debug/debuglog.hpp>

#include "../widget/scenetoolmode.hpp"

#include "../../model/prefs/shortcut.hpp"
#include "../../model/prefs/state.hpp"

#include "cameracontroller.hpp"
#include "mask.hpp"
#include "rendercontext.hpp"

namespace
{
    // A collapsed view would ask for a swapchain with no size.
    constexpr int sMinimumViewSize = 16;

    vsg::vec4 toVec4(const QColor& colour)
    {
        return vsg::vec4(colour.redF(), colour.greenF(), colour.blueF(), 1.f);
    }
}

namespace CSVRender
{
    RenderWidget::RenderWidget(CSMWorld::Data& worldData, QWidget* parent, Qt::WindowFlags f)
        : QWidget(parent, f)
        , mData(worldData)
    {
        setMinimumSize(sMinimumViewSize, sMinimumViewSize);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
    }

    RenderWidget::~RenderWidget()
    {
        // The view first: it takes its window out of the viewer. Qt then destroys the window with the container.
        mHost.reset();
    }

    void RenderWidget::flagAsModified()
    {
        if (mHost)
            mHost->requestRedraw();
    }

    void RenderWidget::setVisibilityMask(unsigned int mask)
    {
        if (mHost)
            mHost->setVisibilityMask(mask);
    }

    bool RenderWidget::ensureHost()
    {
        if (mHost)
            return true;
        if (mHostFailed || !isVisible() || width() <= 0 || height() <= 0)
            return false;

        try
        {
            // One at a time: RenderContext::createWindow explains why.
            mWindow = RenderContext::get().createWindow();
            mWindow->installEventFilter(this);
            // Before initializeWindow: a window takes its swapchain's size from here, and a zero size is refused.
            mWindow->resize(size());
            mContainer = QWidget::createWindowContainer(mWindow, this);
            // Drags reach this widget, not the container: QWidgetWindow walks up to the first that accepts drops.
            mContainer->setAcceptDrops(false);
            mContainer->setFocusPolicy(Qt::ClickFocus);
            layout()->addWidget(mContainer);
            mContainer->show();

            mHost = std::make_unique<SceneHost>(RenderContext::get(), *mWindow, mData);
            updateCameraParameters();
            hostReady();
            mHost->requestRedraw();
            return true;
        }
        catch (const std::exception& e)
        {
            mHostFailed = true;
            Log(Debug::Error) << "Failed to open a 3D view: " << e.what();
            return false;
        }
    }

    void RenderWidget::showEvent(QShowEvent* event)
    {
        QWidget::showEvent(event);
        // After the layout has given this widget its size.
        QTimer::singleShot(0, this, [this] { ensureHost(); });
    }

    void RenderWidget::resizeEvent(QResizeEvent* event)
    {
        QWidget::resizeEvent(event);
        if (!mHost)
            QTimer::singleShot(0, this, [this] { ensureHost(); });
    }

    bool RenderWidget::eventFilter(QObject* watched, QEvent* event)
    {
        // Events over the view go to the window, not to this widget as they did with a QOpenGLWidget child. Give them to
        // this widget too: its handlers and the shortcut system's filter on it expect them. The coordinates mean the same,
        // as the window fills this widget. vsgQt's own handlers still run, but nothing reads the events they buffer.
        if (watched == mWindow)
        {
            switch (event->type())
            {
                case QEvent::MouseButtonPress:
                    // As Qt::ClickFocus on the old child. A child window does not take the focus by itself on every
                    // platform.
                    mWindow->requestActivate();
                    [[fallthrough]];
                case QEvent::MouseMove:
                case QEvent::MouseButtonRelease:
                case QEvent::MouseButtonDblClick:
                case QEvent::Wheel:
                case QEvent::KeyPress:
                case QEvent::KeyRelease:
                case QEvent::FocusIn:
                case QEvent::FocusOut:
                case QEvent::DragEnter:
                case QEvent::DragMove:
                case QEvent::DragLeave:
                case QEvent::Drop:
                    QCoreApplication::sendEvent(this, event);
                    break;
                default:
                    break;
            }
        }
        return QWidget::eventFilter(watched, event);
    }

    void RenderWidget::toggleRenderStats()
    {
        // osgViewer's StatsHandler has no counterpart yet (survey, section 2).
        Log(Debug::Info) << "The render statistics overlay does not exist on the Vulkan build";
    }

    void RenderWidget::updateCameraParameters()
    {
        if (!mHost)
            return;
        const bool ortho = CSMPrefs::get()["Rendering"]["camera-ortho"].isTrue();
        mHost->setProjection(CSMPrefs::get()["Rendering"]["camera-fov"].toInt(), ortho,
            CSMPrefs::get()["Rendering"]["camera-ortho-size"].toInt());
    }

    // ---------------------------------------------------

    SceneWidget::SceneWidget(CSMWorld::Data& worldData, QWidget* parent, Qt::WindowFlags f, bool retrieveInput)
        : RenderWidget(worldData, parent, f)
    {
        mFreeCamControl = new FreeCameraController(this);
        mOrbitCamControl = new OrbitCameraController(this);
        mCurrentCamControl = mFreeCamControl;

        mOrbitCamControl->setPickingMask(Mask_Reference | Mask_Terrain);

        mOrbitCamControl->setConstRoll(CSMPrefs::get()["3D Scene Input"]["navi-orbit-const-roll"].isTrue());

        mBackgroundColour = CSMPrefs::get()["Rendering"]["scene-day-background-colour"].toColor();
        mGradientColour = CSMPrefs::get()["Rendering"]["scene-day-gradient-colour"].toColor();

        // Recieve mouse move event even if mouse button is not pressed
        setMouseTracking(true);
        setFocusPolicy(Qt::ClickFocus);

        connect(&CSMPrefs::State::get(), &CSMPrefs::State::settingChanged, this, &SceneWidget::settingChanged);

        // TODO update this outside of the constructor where virtual methods can be used
        if (retrieveInput)
        {
            CSMPrefs::get()["3D Scene Input"].update();
            CSMPrefs::get()["Tooltips"].update();
        }

        // Shortcuts
        CSMPrefs::Shortcut* focusToolbarShortcut = new CSMPrefs::Shortcut("scene-focus-toolbar", this);
        connect(
            focusToolbarShortcut, qOverload<>(&CSMPrefs::Shortcut::activated), this, &SceneWidget::focusToolbarRequest);

        CSMPrefs::Shortcut* renderStatsShortcut = new CSMPrefs::Shortcut("scene-render-stats", this);
        connect(
            renderStatsShortcut, qOverload<>(&CSMPrefs::Shortcut::activated), this, &SceneWidget::toggleRenderStats);
    }

    SceneWidget::~SceneWidget()
    {
        if (SceneHost* host = getHost())
            host->removeListener(*this);
    }

    void SceneWidget::hostReady()
    {
        applyLighting();
        getHost()->addListener(*this);
        mCurrentCamControl->setCamera(getHost());
    }

    void SceneWidget::setCameraPose(const vsg::dvec3& eye, const vsg::dvec3& center, const vsg::dvec3& up)
    {
        getHost()->setCamera(eye, center, up);
        mCamPositionSet = true;
    }

    void SceneWidget::frameUpdate(float dt)
    {
        // As the OSG editor's SceneWidget::update, with SceneHost::frame in place of CameraController::setup.
        if (mCamPositionSet)
        {
            mCurrentCamControl->update(dt);
            return;
        }
        const std::vector<vsg::dvec3> positions = getDefaultViewPositions();
        if (!positions.empty())
        {
            getHost()->frame(positions);
            mCamPositionSet = true;
        }
    }

    void SceneWidget::applyLighting()
    {
        SceneHost* host = getHost();
        if (!host)
            return;
        const LightingValues values = lightingValues(mLightingMode, mDefaultAmbient, mIsExterior);
        host->setLighting(values);
        host->setBackground(toVec4(mBackgroundColour), toVec4(mGradientColour),
            CSMPrefs::get()["Rendering"]["scene-use-gradient"].isTrue());
        host->setNightDayMode(
            values.mNightDayIndex, CSMPrefs::get()["Rendering"]["scene-day-night-switch-nodes"].isTrue());
    }

    void SceneWidget::selectLightingMode(const std::string& mode)
    {
        if (mode == "day")
            mLightingMode = LightingMode::Day;
        else if (mode == "night")
            mLightingMode = LightingMode::Night;
        else if (mode == "bright")
            mLightingMode = LightingMode::Bright;
        else
            return;
        mBackgroundColour = CSMPrefs::get()["Rendering"]["scene-" + mode + "-background-colour"].toColor();
        mGradientColour = CSMPrefs::get()["Rendering"]["scene-" + mode + "-gradient-colour"].toColor();
        applyLighting();
        flagAsModified();
    }

    CSVWidget::SceneToolMode* SceneWidget::makeLightingSelector(CSVWidget::SceneToolbar* parent)
    {
        CSVWidget::SceneToolMode* tool = new CSVWidget::SceneToolMode(parent, "Lighting Mode");

        /// \todo replace icons
        tool->addButton(":scenetoolbar/day", "day",
            "Day"
            "<ul><li>Cell specific ambient in interiors</li>"
            "<li>Low ambient in exteriors</li>"
            "<li>Strong directional light source</li>"
            "<li>This mode closely resembles day time in-game</li></ul>");
        tool->addButton(":scenetoolbar/night", "night",
            "Night"
            "<ul><li>Cell specific ambient in interiors</li>"
            "<li>Low ambient in exteriors</li>"
            "<li>Weak directional light source</li>"
            "<li>This mode closely resembles night time in-game</li></ul>");
        tool->addButton(":scenetoolbar/bright", "bright",
            "Bright"
            "<ul><li>Maximum ambient</li>"
            "<li>Strong directional light source</li></ul>");

        connect(tool, &CSVWidget::SceneToolMode::modeChanged, this, &SceneWidget::selectLightingMode);

        return tool;
    }

    void SceneWidget::setDefaultAmbient(const vsg::vec4& colour)
    {
        mDefaultAmbient = colour;
        applyLighting();
    }

    void SceneWidget::setExterior(bool isExterior)
    {
        mIsExterior = isExterior;
        applyLighting();
    }

    void SceneWidget::mouseMoveEvent(QMouseEvent* event)
    {
        const QPointF position = event->position();
        mCurrentCamControl->handleMouseMoveEvent(position.x() - mPrevMouse.x(), position.y() - mPrevMouse.y());
        mPrevMouse = position;
    }

    void SceneWidget::wheelEvent(QWheelEvent* event)
    {
        mCurrentCamControl->handleMouseScrollEvent(event->angleDelta().y());
    }

    void SceneWidget::settingChanged(const CSMPrefs::Setting* setting)
    {
        if (*setting == "3D Scene Input/p-navi-free-sensitivity")
        {
            mFreeCamControl->setCameraSensitivity(setting->toDouble());
        }
        else         if (*setting == "3D Scene Input/p-navi-orbit-sensitivity")
        {
            mOrbitCamControl->setCameraSensitivity(setting->toDouble());
        }
        else         if (*setting == "3D Scene Input/p-navi-free-invert")
        {
            mFreeCamControl->setInverted(setting->isTrue());
        }
        else         if (*setting == "3D Scene Input/p-navi-orbit-invert")
        {
            mOrbitCamControl->setInverted(setting->isTrue());
        }
        else         if (*setting == "3D Scene Input/s-navi-sensitivity")
        {
            mFreeCamControl->setSecondaryMovementMultiplier(setting->toDouble());
            mOrbitCamControl->setSecondaryMovementMultiplier(setting->toDouble());
        }
        else         if (*setting == "3D Scene Input/navi-wheel-factor")
        {
            mFreeCamControl->setWheelMovementMultiplier(setting->toDouble());
            mOrbitCamControl->setWheelMovementMultiplier(setting->toDouble());
        }
        else         if (*setting == "3D Scene Input/navi-free-lin-speed")
        {
            mFreeCamControl->setLinearSpeed(setting->toDouble());
        }
        else         if (*setting == "3D Scene Input/navi-free-rot-speed")
        {
            mFreeCamControl->setRotationalSpeed(setting->toDouble());
        }
        else         if (*setting == "3D Scene Input/navi-free-speed-mult")
        {
            mFreeCamControl->setSpeedMultiplier(setting->toDouble());
        }
        else         if (*setting == "3D Scene Input/navi-orbit-rot-speed")
        {
            mOrbitCamControl->setOrbitSpeed(setting->toDouble());
        }
        else         if (*setting == "3D Scene Input/navi-orbit-speed-mult")
        {
            mOrbitCamControl->setOrbitSpeedMultiplier(setting->toDouble());
        }
        else         if (*setting == "3D Scene Input/navi-orbit-const-roll")
        {
            mOrbitCamControl->setConstRoll(setting->isTrue());
        }
        else if (*setting == "Rendering/framerate-limit")
        {
            RenderContext::get().setFrameRateLimit(setting->toInt());
        }
        else if (*setting == "Rendering/camera-fov" || *setting == "Rendering/camera-ortho"
            || *setting == "Rendering/camera-ortho-size")
        {
            updateCameraParameters();
        }
        else if (*setting == "Rendering/scene-day-night-switch-nodes" || *setting == "Rendering/scene-use-gradient")
        {
            applyLighting();
        }
        flagAsModified();
    }

    void SceneWidget::selectNavigationMode(const std::string& mode)
    {
        // Until the view exists, setCamera(nullptr) on the old controller and a null host on the new leave them inactive;
        // hostReady activates whichever is current.
        mCurrentCamControl->setCamera(nullptr);
        if (mode == "orbit")
        {
            mCurrentCamControl = mOrbitCamControl;
            mOrbitCamControl->setCamera(getHost());
            mOrbitCamControl->reset();
            return;
        }
        mCurrentCamControl = mFreeCamControl;
        mFreeCamControl->setCamera(getHost());
        if (mode == "1st")
            mFreeCamControl->fixUpAxis(CameraController::WorldUp);
        else if (mode == "free")
            mFreeCamControl->unfixUpAxis();
    }
}
