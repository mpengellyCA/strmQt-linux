#pragma once

#include "input/InputMap.h"

#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QObject>
#include <QPointer>

// Makes a TV remote's Back button mean exactly what Esc means.
//
// A remote's Back sends KEY_BACK, which Qt names Qt::Key_Back. nav.back lists it,
// but a binding alone reached only the one handler that asks the input map —
// Main.qml's StackView Keys.onPressed. Everything else that "goes back" answers
// Esc by name: Keys.onEscapePressed on every panel, menu, picker and dialog, and
// Popup's CloseOnEscape. So Back walked the page history and could not close a
// menu, which is where a remote user most needs it.
//
// Rewriting at the window is the same answer the gamepad already gets: its B
// button delivers the key nav.back is PRIMARILY bound to (NavigationKeyHandler),
// and so does this. Any other key bound to nav.back that a text field would not
// use itself is rewritten the same way — so rebinding moves the remote with it —
// while Backspace, which is typable, stays Backspace and keeps editing text.
//
// Qt matches window Shortcuts before a key reaches this filter, so a Shortcut
// bound to Back keeps it. In the player, where Back is also a player.minimize
// binding, arriving as Esc is equivalent: Esc is bound to the same action.
namespace strmqt {

class RemoteBackKeyFilter final : public QObject
{
public:
    RemoteBackKeyFilter(const InputMap *input, QObject *parent)
        : QObject(parent), m_input(input)
    {
    }

    // The key a press of `key` should be delivered as, or 0 to leave it alone.
    static int rewriteFor(const InputMap *input, int key, int modifiers)
    {
        if (!input || key == 0 || key == Qt::Key_unknown)
            return 0;
        const QString action = QStringLiteral("nav.back");
        const int primary = input->keyFor(action);
        if (primary == 0 || primary == key)
            return 0;
        const QString sequence = input->sequenceForKey(key, modifiers);
        if (sequence.isEmpty() || input->isTypableSequence(sequence))
            return 0;
        return input->bindings(action).contains(sequence) ? primary : 0;
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease)
            return QObject::eventFilter(watched, event);

        auto *key = static_cast<QKeyEvent *>(event);
        const int target = rewriteFor(m_input, key->key(), int(key->modifiers()));
        if (target == 0)
            return QObject::eventFilter(watched, event);

        const auto modifiers =
            static_cast<Qt::KeyboardModifiers>(m_input->modifiersFor(QStringLiteral("nav.back")));
        QKeyEvent back(key->type(), target, modifiers, key->nativeScanCode(),
                       key->nativeVirtualKey(), key->nativeModifiers(), QString(),
                       key->isAutoRepeat(), key->count());
        QCoreApplication::sendEvent(watched, &back);
        return true;
    }

private:
    QPointer<const InputMap> m_input;
};

} // namespace strmqt
