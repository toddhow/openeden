#include "quietwindow.hpp"

#include <cmath>
#include <string_view>

namespace
{
    // The game's position (apps/openmw/vsgengine.cpp, Engine::createRenderEngine), in device pixels: far enough to be
    // off every monitor layout, and within the -32768 to 32767 that WM_MOVE and similar messages carry.
    constexpr double sQuietWindowDevicePixels = -30000.0;
}

bool CSMPrefs::isQuietWindowValue(const char* value)
{
    if (value == nullptr)
        return false;
    const std::string_view text(value);
    return !text.empty() && text != "0";
}

QPoint CSMPrefs::quietWindowPosition(double devicePixelRatio)
{
    const double ratio = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
    const int coordinate = static_cast<int>(std::lround(sQuietWindowDevicePixels / ratio));
    return QPoint(coordinate, coordinate);
}

bool CSMPrefs::isOnAnyScreen(const QRect& frame, const std::vector<QRect>& screens)
{
    for (const QRect& screen : screens)
        if (frame.intersects(screen))
            return true;
    return false;
}

Qt::WindowFlags CSMPrefs::quietWindowFlags(Qt::WindowFlags flags, bool hasTransientParent)
{
    const auto type = static_cast<Qt::WindowType>(flags.toInt() & Qt::WindowType_Mask);
    switch (type)
    {
        case Qt::Window:
        case Qt::Dialog:
        case Qt::Sheet:
            // The kinds Qt's Windows plugin creates without WS_EX_TOOLWINDOW (qwindowswindow.cpp,
            // WindowCreationData::fromWindow); unowned, Windows gives them a taskbar button.
            if (!hasTransientParent)
                flags = (flags & ~Qt::WindowFlags(Qt::WindowType_Mask)) | Qt::Tool;
            return flags | Qt::WindowDoesNotAcceptFocus;
        case Qt::Tool:
        case Qt::Drawer:
        case Qt::SplashScreen:
            return flags | Qt::WindowDoesNotAcceptFocus;
        default:
            // Popups and tooltips: Qt shows them with SW_SHOWNOACTIVATE and WS_EX_TOOLWINDOW already
            // (QWindowsWindow::show_sys, WindowCreationData::fromWindow). The rest are not top-level windows.
            return flags;
    }
}

Qt::WindowStates CSMPrefs::quietWindowStates(Qt::WindowStates states)
{
    return states & ~(Qt::WindowMinimized | Qt::WindowMaximized | Qt::WindowFullScreen);
}
