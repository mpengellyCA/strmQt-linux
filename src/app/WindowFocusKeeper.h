#pragma once

#include <QEvent>
#include <QObject>

// Keeps the focused item focused while the window is in the background.
//
// When another application takes activation, Qt sends the window a FocusOut
// with Qt::ActiveWindowFocusReason and QQuickWindow answers it by clearing the
// focus chain: activeFocusItem becomes null until the window is activated
// again. For a keyboard that is harmless — its keys go to the other app anyway.
// For the web remote and the gamepad it is fatal: both post key events to this
// window without needing it to be active, and a window with no active focus
// item delivers them to nothing. Inside the app a remote "Home" happened to
// re-seat focus by moving to a page; the sign-in screen had nothing that did,
// so one click on another window stranded the remote there.
//
// Swallowing the window's FocusOut leaves the chain exactly as it was. The
// reason cannot be relied on: the offscreen and X11 platforms report
// Qt::ActiveWindowFocusReason, but KWin on Wayland reports
// Qt::OtherFocusReason, so every FocusOut sent to the window is taken. That is
// safe because a window only receives one when it stops being the focus window;
// focus moving between items inside it never produces one. Activation state
// (QWindow::active, Window.active in QML) is not carried by this event and still
// changes.
//
// Measured on Qt 6.11.2 in tests/unit/tst_window_focus_keeper.cpp, and the
// Wayland reason on KWin 6 with a probe window.
namespace strmqt {

class WindowFocusKeeper final : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::FocusOut)
            return true;
        return QObject::eventFilter(watched, event);
    }
};

} // namespace strmqt
