#pragma once

#include <QCoreApplication>
#include <QGuiApplication>
#include <QInputMethodQueryEvent>
#include <QKeyEvent>
#include <QWindow>

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

// The text is deliberately empty: this is a command, not typing.
inline void postKey(QWindow *window, int key, int modifiers, bool pressed, bool autoRepeat)
{
    QCoreApplication::postEvent(window,
                                new QKeyEvent(pressed ? QEvent::KeyPress : QEvent::KeyRelease, key,
                                              static_cast<Qt::KeyboardModifiers>(modifiers),
                                              QString(), autoRepeat));
}

} // namespace strmqt::keydelivery
