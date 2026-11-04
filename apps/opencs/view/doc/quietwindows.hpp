#ifndef CSV_DOC_QUIETWINDOWS_H
#define CSV_DOC_QUIETWINDOWS_H

#include <QObject>

class QApplication;
class QEvent;
class QRect;
class QWidget;
class QWindow;

namespace CSVDoc
{
    /// OPENEDEN_QUIET_WINDOW for Eden Studio, as the game has it (apps/openmw/vsgengine.cpp): with the variable set to
    /// anything but empty or "0", every top-level window the editor shows (document windows, dialogues, message boxes,
    /// the loading window, a capture's frame, floating docks, popups) opens at the game's far-off position, out of the
    /// taskbar and without taking the focus. It is not minimised: a minimised window has a 0x0 swapchain and draws
    /// nothing. Unset, empty or "0": nothing is installed and every window behaves as before.
    ///
    /// An application event filter, so it sees each window before Qt shows it natively. A top-level widget is moved off
    /// as soon as it has a native window (QEvent::WinIdChange, from QWidgetPrivate::create) and again at QEvent::Show,
    /// which QWidgetPrivate::show_helper sends before show_sys. A created widget whose flags change is created again
    /// later, and if it is visible by then, QWidgetPrivate::create shows its window at once, before any QEvent::Show
    /// reaches the widget: the creation event covers that, and the QEvent::Show that QWindowPrivate::setVisible sends
    /// to every QWindow before the platform shows it is the last check. An application filter runs before the widget's
    /// own event handler, so the move also comes before QDialog::showEvent, which would otherwise centre a dialogue and
    /// clamp it to the screen (QDialog::adjustPosition; it is skipped for a widget with Qt::WA_Moved). A window that is
    /// moved or grows back onto a screen while shown (a restored floating dock, code that keeps a window inside the
    /// screen) is moved off again.
    ///
    /// Owned by whoever creates it (main.cpp keeps one on the stack, after the application and before every window);
    /// QObject's destructor takes it out of the application's filters.
    class QuietWindows : public QObject
    {
    public:
        QuietWindows() = default;

        /// Installs this filter on \a application when OPENEDEN_QUIET_WINDOW asks for quiet windows. Returns whether
        /// it did.
        bool installIfRequested(QApplication& application);

        bool eventFilter(QObject* watched, QEvent* event) override;

    private:
        /// No activation and no taskbar button, on the widget and on its QWindow (see CSMPrefs::quietWindowFlags).
        static void prepare(QWidget& window);
        static void prepare(QWindow& window);

        static void moveOffScreen(QWidget& window);

        /// Whether \a frame overlaps any of the application's screens.
        static bool isOnScreen(const QRect& frame);
    };
}

#endif
