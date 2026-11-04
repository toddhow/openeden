#ifndef CSM_PREFS_QUIETWINDOW_H
#define CSM_PREFS_QUIETWINDOW_H

#include <vector>

#include <QPoint>
#include <QRect>
#include <Qt>

#include <components/misc/quietwindow.hpp>

namespace CSMPrefs
{
    /// The variable that asks for quiet windows, the same as the game's (apps/openmw/vsgengine.cpp,
    /// Engine::createRenderEngine): test and check runs set it so that no window appears on the desktop.
    inline constexpr const char* quietWindowVariable = Misc::quietWindowVariable;

    /// Whether a value of OPENEDEN_QUIET_WINDOW asks for quiet windows, read as the game and the crash catcher read it
    /// (Misc::isQuietWindowValue): unset (null), empty or exactly "0" means off, anything else on.
    bool isQuietWindowValue(const char* value);

    /// Where a quiet window goes, in Qt's device-independent pixels: the game's (-30000, -30000) in device pixels, far
    /// off every monitor layout. Dividing by the screen's \a devicePixelRatio keeps it within the 16-bit coordinates
    /// Windows' window messages carry on a scaled screen; a ratio that is not positive counts as 1.
    QPoint quietWindowPosition(double devicePixelRatio);

    /// Whether a window's frame overlaps any of \a screens (Qt's screen geometries), so that a quiet window must be
    /// moved off again. A frame that only touches a screen's edge does not overlap it.
    bool isOnAnyScreen(const QRect& frame, const std::vector<QRect>& screens);

    /// The flags a quiet top-level window's QWindow gets, from the ones it has. Every window that can be activated
    /// gets Qt::WindowDoesNotAcceptFocus, which makes QWindow::requestActivate do nothing (activateWindow() calls
    /// included). An ordinary window, dialog or sheet with no transient parent is the kind Windows gives a taskbar
    /// button; it becomes a Qt::Tool window, which Qt's Windows plugin creates with WS_EX_TOOLWINDOW (no button) and
    /// shows without activating. Popups and tooltips are left as they are: Qt already shows them without activation
    /// and without a button. A child widget's flags (no Qt::Window bit) are returned unchanged.
    Qt::WindowFlags quietWindowFlags(Qt::WindowFlags flags, bool hasTransientParent);

    /// The states a quiet window may be shown in: a maximised or full-screen window ignores its position, and a
    /// minimised one has a 0x0 swapchain and draws nothing, so those three are taken off.
    Qt::WindowStates quietWindowStates(Qt::WindowStates states);
}

#endif
