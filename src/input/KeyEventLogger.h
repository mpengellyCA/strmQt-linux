#pragma once

#include "core/Log.h"

#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QObject>
#include <QString>

// Logs every key the app window receives, with the native codes a remote's
// odd button can only be recognised by (strmqt.input.keys, off by default).
//
// Installed on the application rather than the window, and only when the
// category is enabled at startup (main.cpp), so it costs nothing otherwise and
// runs before every other filter — the OK and Back rewrites included, which
// then show up as a second line carrying the key they were rewritten to.
//
// Two kinds of line, because Qt asks about shortcuts before it delivers a key:
//
//   check    the ShortcutOverride sent to the focused item for a press. A
//            press that a Shortcut then consumed has this line and no "press".
//   press /  the key event as the window received it.
//   release
//
// Each carries the Qt key (hex, and QKeySequence's name when it has one), the
// modifiers, the native scan code (the kernel keycode + 8 under X11/Wayland)
// and the native keysym (the XKB name a remote's button maps to).
namespace strmqt {

class KeyEventLogger final : public QObject
{
public:
    using QObject::QObject;

    static QString describe(const QKeyEvent *event)
    {
        const char *kind = event->type() == QEvent::ShortcutOverride ? "check  "
                           : event->type() == QEvent::KeyPress        ? "press  "
                                                                      : "release";
        const QString name = event->key() == 0 || event->key() == Qt::Key_unknown
                                 ? QStringLiteral("(no Qt key)")
                                 : QKeySequence(event->key()).toString(QKeySequence::PortableText);
        // A remote's Return arrives with "\r" as its text; keep the line whole.
        QString text;
        for (const QChar c : event->text())
            text += c.isPrint() ? QString(c)
                                : QStringLiteral("\\u%1").arg(uint(c.unicode()), 4, 16, QLatin1Char('0'));
        return QStringLiteral("%1 key=0x%2 \"%3\" mods=0x%4 scan=%5 keysym=0x%6 text=\"%7\"%8")
            .arg(QLatin1String(kind))
            .arg(event->key(), 0, 16)
            .arg(name)
            .arg(uint(event->modifiers()), 0, 16)
            .arg(event->nativeScanCode())
            .arg(event->nativeVirtualKey(), 0, 16)
            .arg(text)
            .arg(event->isAutoRepeat() ? QStringLiteral(" repeat") : QString());
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        const QEvent::Type type = event->type();
        const bool keyAtWindow =
            (type == QEvent::KeyPress || type == QEvent::KeyRelease) && watched->isWindowType();
        if (keyAtWindow || type == QEvent::ShortcutOverride) {
            const auto *key = static_cast<const QKeyEvent *>(event);
            // The same override can be handed up the item tree; say it once.
            const Seen seen{type, key->key(), key->timestamp(), key->isAutoRepeat()};
            if (!(type == QEvent::ShortcutOverride && seen == m_last))
                qCDebug(logKeys).noquote() << describe(key);
            m_last = seen;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    struct Seen
    {
        QEvent::Type type = QEvent::None;
        int key = 0;
        quint64 timestamp = 0;
        bool repeat = false;
        bool operator==(const Seen &) const = default;
    };
    Seen m_last;
};

} // namespace strmqt
