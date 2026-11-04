#include "quietwindows.hpp"

#include <cstdlib>
#include <vector>

#include <QApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QVariant>
#include <QWidget>
#include <QWindow>

#include <components/debug/debuglog.hpp>

#include "../../model/prefs/quietwindow.hpp"

namespace
{
    // The QWindow property that Qt::WA_ShowWithoutActivating becomes: QWidgetPrivate::create copies the attribute to
    // it, and the platform plugins read it when they show the window (qwindowswindow.cpp, testShowWithoutActivating,
    // which picks SW_SHOWNOACTIVATE; qxcbwindow.cpp, QXcbWindow::show). It is set here directly because a widget's
    // QWindow already exists when QEvent::Show arrives (QWidgetPrivate::setVisible creates it first).
    constexpr const char* sShowWithoutActivatingProperty = "_q_showWithoutActivating";
}

bool CSVDoc::QuietWindows::installIfRequested(QApplication& application)
{
    if (!CSMPrefs::isQuietWindowValue(std::getenv(CSMPrefs::quietWindowVariable)))
        return false;
    application.installEventFilter(this);
    Log(Debug::Info) << CSMPrefs::quietWindowVariable
                     << " is set: windows open off the desktop, out of the taskbar and without taking the focus";
    return true;
}

bool CSVDoc::QuietWindows::eventFilter(QObject* watched, QEvent* event)
{
    const QEvent::Type type = event->type();
    if (type != QEvent::Show && type != QEvent::WinIdChange && type != QEvent::Move && type != QEvent::Resize)
        return false;

    if (watched->isWindowType())
    {
        // The last moment before the platform shows a window: QWindowPrivate::setVisible sends QEvent::Show to the
        // QWindow before QPlatformWindow::setVisible, and a widget's window comes through here too, from
        // QWidgetPrivate::show_sys and from QWidgetPrivate::create, which shows the window of a widget that is already
        // visible (a top-level created again while QWidgetPrivate::show_helper shows its children). The widget's own
        // events below normally have done everything by then.
        QWindow& window = *static_cast<QWindow*>(watched);
        if (type != QEvent::Show || !window.isTopLevel() || window.type() == Qt::Desktop)
            return false;
        if (QWidget* widget = QWidget::find(window.winId()); widget != nullptr && widget->isWindow())
        {
            prepare(*widget);
            if (isOnScreen(widget->frameGeometry()))
                moveOffScreen(*widget);
        }
        else
        {
            prepare(window);
            if (isOnScreen(window.frameGeometry()))
                window.setFramePosition(CSMPrefs::quietWindowPosition(window.devicePixelRatio()));
        }
        return false;
    }

    if (!watched->isWidgetType())
        return false;
    QWidget& widget = *static_cast<QWidget*>(watched);
    if (!widget.isWindow() || widget.windowType() == Qt::Desktop)
        return false;

    switch (type)
    {
        case QEvent::WinIdChange:
            // The widget has a native window now (QWidgetPrivate::create), first or again: a created widget whose flags
            // change is created again (QWidgetPrivate::setParent_sys clears Qt::WA_WState_Created), which gives its
            // QWindow the widget's own flags back and, when the widget is already visible, shows the window at once,
            // before any QEvent::Show reaches the widget. So the flags and the position go on here as well.
            if (widget.internalWinId() == 0)
                break;
            prepare(widget);
            if (isOnScreen(widget.frameGeometry()))
                moveOffScreen(widget);
            break;
        case QEvent::Show:
        {
            // Before show_sys gives the window to the platform (QWidgetPrivate::show_helper sends this event first),
            // and before the widget's own showEvent, so a dialogue keeps this position (see moveOffScreen).
            prepare(widget);
            const Qt::WindowStates states = CSMPrefs::quietWindowStates(widget.windowState());
            if (states != widget.windowState())
                widget.setWindowState(states);
            moveOffScreen(widget);
            break;
        }
        default:
            // Move or Resize: a shown window that something put back on a screen, or that grew onto one.
            if (widget.isVisible() && isOnScreen(widget.frameGeometry()))
                moveOffScreen(widget);
            break;
    }
    return false;
}

void CSVDoc::QuietWindows::prepare(QWidget& window)
{
    // For a QWindow that Qt makes later (QWidgetPrivate::create copies the attribute to the property).
    window.setAttribute(Qt::WA_ShowWithoutActivating);
    // On the QWindow only: the widget keeps its own type, so Qt::WA_QuitOnClose (QWidgetPrivate::
    // adjustQuitOnCloseAttribute) and QWidget::isActiveWindow, which decides the palette widgets are drawn with, stay
    // as they were.
    if (QWindow* handle = window.windowHandle())
        prepare(*handle);
}

void CSVDoc::QuietWindows::prepare(QWindow& window)
{
    window.setProperty(sShowWithoutActivatingProperty, true);
    const Qt::WindowFlags flags = CSMPrefs::quietWindowFlags(window.flags(), window.transientParent() != nullptr);
    if (flags != window.flags())
        window.setFlags(flags);
}

void CSVDoc::QuietWindows::moveOffScreen(QWidget& window)
{
    // QWidget::move sets Qt::WA_Moved, so QDialog::showEvent does not centre the dialogue and clamp it to the screen
    // (QDialog::adjustPosition), and show_sys gives the window this position (QWidgetPrivate::show_sys). Qt's Windows
    // plugin moves a created window with MoveWindow, which does not clamp (QWindowsBaseWindow::setGeometry_sys).
    const QPoint position = CSMPrefs::quietWindowPosition(window.devicePixelRatioF());
    if (window.pos() != position)
        window.move(position);
}

bool CSVDoc::QuietWindows::isOnScreen(const QRect& frame)
{
    std::vector<QRect> screens;
    for (const QScreen* screen : QGuiApplication::screens())
        screens.push_back(screen->geometry());
    return CSMPrefs::isOnAnyScreen(frame, screens);
}
