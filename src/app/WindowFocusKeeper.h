#pragma once

#include <QCoreApplication>
#include <QEvent>
#include <QHash>
#include <QKeyEvent>
#include <QObject>

#include <utility>

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
// One thing the cleared chain used to do is lost with it, and is put back here.
// A keyboard key held down while the window is deactivated is released into the
// other application, so its release never arrives. Qt does not make one up
// either — QQuickWindow::focusOutEvent only drops focus — but dropping focus
// was a signal a control could end a hold on (StrmSlider disarms on it), and
// with focus kept, a control that commits on release waits for a release that
// is not coming. So the keeper remembers which keys the keyboard is holding and
// releases them itself when the window stops being the focus window. Only the
// keyboard's: its events are the spontaneous ones. A direction the gamepad is
// holding is posted, not spontaneous, is still held, and will be released by
// the pad — ending it here would cut a background scrub short.
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
        switch (event->type()) {
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
            noteKey(static_cast<QKeyEvent *>(event));
            break;
        case QEvent::FocusOut:
            releaseHeldKeys(watched);
            return true;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void noteKey(const QKeyEvent *key)
    {
        // An auto-repeat is a release/press pair for a key that never came up.
        // Key 0 is a keysym Qt has no name for (RemoteOkKeyFilter.h); there is
        // nothing to tell two of those apart by.
        if (!key->spontaneous() || key->isAutoRepeat() || key->key() == 0
            || key->key() == Qt::Key_unknown)
            return;
        if (key->type() == QEvent::KeyPress)
            m_held.insert(key->key(), key->modifiers());
        else
            m_held.remove(key->key());
    }

    void releaseHeldKeys(QObject *window)
    {
        // Taken first: the releases come back through this filter.
        const QHash<int, Qt::KeyboardModifiers> held = std::exchange(m_held, {});
        for (auto it = held.cbegin(); it != held.cend(); ++it) {
            QKeyEvent release(QEvent::KeyRelease, it.key(), it.value());
            QCoreApplication::sendEvent(window, &release);
        }
    }

    QHash<int, Qt::KeyboardModifiers> m_held;
};

} // namespace strmqt
