#pragma once

#include <QEvent>
#include <QFocusEvent>
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
// Swallowing that one FocusOut leaves the chain exactly as it was. Focus moves
// inside the window, and a FocusOut for any other reason, pass through
// untouched; activation state (QWindow::active, Window.active in QML) is not
// carried by this event and still changes.
//
// Measured on Qt 6.11.2 in tests/unit/tst_window_focus_keeper.cpp.
namespace strmqt {

class WindowFocusKeeper final : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::FocusOut
            && static_cast<QFocusEvent *>(event)->reason() == Qt::ActiveWindowFocusReason)
            return true;
        return QObject::eventFilter(watched, event);
    }
};

} // namespace strmqt
