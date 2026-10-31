#ifndef OPENCS_VIEW_SCENEWIDGET_H
#define OPENCS_VIEW_SCENEWIDGET_H

#include <memory>
#include <optional>
#include <string>

#include <QColor>
#include <QPointF>
#include <QWidget>

#include <vsg/maths/vec4.h>

#include "lighting.hpp"
#include "scenehost.hpp"

class QMouseEvent;
class QWheelEvent;

namespace CSVWidget
{
    class SceneToolMode;
    class SceneToolbar;
}

namespace CSMPrefs
{
    class Setting;
}

namespace CSMWorld
{
    class Data;
}

namespace vsgQt
{
    class Window;
}

namespace CSVRender
{
    /// A QWidget that shows one SceneHost. The Vulkan window is a child made through QWidget::createWindowContainer the
    /// first time this widget has a size: a window with no size cannot be hosted. Whatever needs the host waits for
    /// hostReady(). Owns the host and, through Qt, the window.
    class RenderWidget : public QWidget
    {
        Q_OBJECT

    public:
        RenderWidget(CSMWorld::Data& data, QWidget* parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());
        ~RenderWidget() override;

        /// Initiates a request to redraw the view
        void flagAsModified();

        void setVisibilityMask(unsigned int mask);

        /// The view, or null before this widget first had a size.
        SceneHost* getHost() const { return mHost.get(); }

        /// Creates the view if this widget has a size and is shown.
        /// \return whether the view exists now.
        bool ensureHost();

    protected:
        /// The view exists now. Called once, from ensureHost.
        virtual void hostReady() {}

        void updateCameraParameters();

        void showEvent(QShowEvent* event) override;
        void resizeEvent(QResizeEvent* event) override;
        bool eventFilter(QObject* watched, QEvent* event) override;

        CSMWorld::Data& mData;

    protected slots:

        void toggleRenderStats();

    private:
        vsgQt::Window* mWindow = nullptr;
        QWidget* mContainer = nullptr;
        std::unique_ptr<SceneHost> mHost;
        bool mHostFailed = false;
    };

    /// Extension of RenderWidget to support lighting mode selection & toolbar
    class SceneWidget : public RenderWidget
    {
        Q_OBJECT
    public:
        SceneWidget(CSMWorld::Data& data, QWidget* parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags(),
            bool retrieveInput = true);
        ~SceneWidget() override;

        CSVWidget::SceneToolMode* makeLightingSelector(CSVWidget::SceneToolbar* parent);
        ///< \attention The created tool is not added to the toolbar (via addTool). Doing that
        /// is the responsibility of the calling function.

        void setDefaultAmbient(const vsg::vec4& colour);
        ///< \note The actual ambient colour may differ based on lighting settings.

        void setExterior(bool isExterior);

    protected:
        void hostReady() override;

        void mouseMoveEvent(QMouseEvent* event) override;
        void wheelEvent(QWheelEvent* event) override;

        void applyLighting();

        LightingMode mLightingMode = LightingMode::Day;
        std::optional<vsg::vec4> mDefaultAmbient;
        bool mIsExterior = true;
        QColor mBackgroundColour;
        QColor mGradientColour;

        QPointF mPrevMouse;

        /// Tells update that camera isn't set
        bool mCamPositionSet = false;

    public slots:

        /// The same as choosing a mode on the toolbar: "day", "night" or "bright".
        void selectLightingMode(const std::string& mode);

    protected slots:

        virtual void settingChanged(const CSMPrefs::Setting* setting);

        void selectNavigationMode(const std::string& mode);

    signals:

        void focusToolbarRequest();
    };
}

#endif
