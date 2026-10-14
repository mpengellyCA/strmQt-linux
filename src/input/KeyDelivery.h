#pragma once

#include <QCoreApplication>
#include <QGuiApplication>
#include <QInputMethodQueryEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QWindow>

#include "input/InputMap.h"

// Delivering a key on behalf of an input that is not the keyboard — the web
// remote and the gamepad — to the app window, whether or not it is active.
//
// The phone is in someone's hand and the pad is on the sofa, so the window is
// often behind another one: focusWindow() is then null, and dropping the key
// there is what made both go dead the moment anything else was clicked.
// WindowFocusKeeper keeps the window's focused item through that, so a key
// posted to it lands where it would have if the window were active.
namespace strmqt::keydelivery {

inline QWindow *targetWindow()
{
    if (QWindow *focused = QGuiApplication::focusWindow())
        return focused;
    const QWindowList windows = QGuiApplication::topLevelWindows();
    for (QWindow *candidate : windows) {
        if (candidate->isVisible() && candidate->type() == Qt::Window)
            return candidate;
    }
    return nullptr;
}

// Asked of the window's own focus object rather than the application's, which
// is null whenever another application has focus — and a search box that still
// holds the window's focus would then be typed into by a "command".
inline bool textInputFocused(QWindow *window)
{
    QObject *focus = window ? window->focusObject() : nullptr;
    if (!focus)
        return false;
    QInputMethodQueryEvent query(Qt::ImEnabled);
    QCoreApplication::sendEvent(focus, &query);
    return query.value(Qt::ImEnabled).toBool();
}

// A key that did not come from a keyboard. Its own type so that it can be told
// apart where it lands (DeliveredKeyStamp below) without guessing from timing.
class DeliveredKey final : public QKeyEvent
{
public:
    using QKeyEvent::QKeyEvent;
};

// The text is deliberately empty: this is a command, not typing.
inline void postKey(QWindow *window, int key, int modifiers, bool pressed, bool autoRepeat)
{
    QCoreApplication::postEvent(window,
                                new DeliveredKey(pressed ? QEvent::KeyPress : QEvent::KeyRelease,
                                                 key, static_cast<Qt::KeyboardModifiers>(modifiers),
                                                 QString(), autoRepeat));
}

// The shell and the player stamp "keyboard" as the device in use from their
// key handlers, and a key delivered here arrives in those same handlers — so
// every D-pad press was stamped "gamepad" by the pad and "keyboard" a moment
// later by the page it landed on. The on-screen hints followed, and each flip
// was a settings write. While a delivered key is being handled, a "keyboard"
// stamp is not believed.
//
// The hold is let go on the next turn of the loop, because a filter is told
// when delivery starts and never when it ends. That leaves it standing for
// whatever else is dispatched in the same turn, and a keystroke from the real
// keyboard queued behind the pad's would not be believed either. So a key that
// came from the window system ends the hold at once: nothing delivered here is
// spontaneous, and nothing a keyboard sends is not. The test is not "any key
// that is not a DeliveredKey" — RemoteOkKeyFilter and RemoteBackKeyFilter
// answer a delivered key by sending a plain QKeyEvent in its place, from
// inside its delivery, and that one is still the pad's.
//
// A keystroke Qt matches to a window Shortcut is not seen here at all — it is
// taken before any event reaches the window — so that one case still waits for
// the timer.
class DeliveredKeyStamp final : public QObject
{
public:
    DeliveredKeyStamp(InputMap *input, QObject *parent)
        : QObject(parent), m_input(input)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if ((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
            && m_input) {
            if (dynamic_cast<DeliveredKey *>(event)) {
                m_input->setKeyboardStampHeld(true);
                // Delivery is synchronous from here; the next turn of the loop
                // is after every handler has run.
                QTimer::singleShot(0, m_input, [input = m_input] {
                    input->setKeyboardStampHeld(false);
                });
            } else if (event->spontaneous()) {
                m_input->setKeyboardStampHeld(false);
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    InputMap *m_input = nullptr;
};

} // namespace strmqt::keydelivery
