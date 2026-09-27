#pragma once

#include "core/Log.h"

#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QObject>

// Makes a TV remote's OK button mean Return.
//
// The app's "Select" is Return / Enter / Space (InputMap nav.select), and every
// grid, rail, card and button answers Keys.onReturnPressed. Bluetooth remotes in
// D-pad mode rarely send any of those for OK: the kernel reports KEY_SELECT or
// KEY_OK. Qt 6.11.2 turns KEY_SELECT (keysym XF86Select) into Qt::Key_Select,
// which nothing handles, and has no key at all for KEY_OK (keysym XF86OK): the
// event arrives with key 0, recognisable only by its native keysym. So OK did
// nothing until the remote was switched to air-mouse mode, where it clicks.
//
// Rewriting both into Return at the window keeps every page's existing handling
// and needs no binding of its own. Any other key Qt cannot name is logged with
// its native codes, which is how the next odd remote gets identified.
//
// Mapping measured with QXkbCommon::keysymToQtKey on Qt 6.11.2 / xkbcommon.
namespace strmqt {

class RemoteOkKeyFilter final : public QObject
{
public:
    using QObject::QObject;

    // XKB_KEY_XF86OK, without pulling xkbcommon into the build for one constant.
    static constexpr quint32 kXF86OkKeysym = 0x10081160;

    static bool isRemoteOk(const QKeyEvent *event)
    {
        return event->key() == Qt::Key_Select
               || (event->key() == 0 && event->nativeVirtualKey() == kXF86OkKeysym);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease)
            return QObject::eventFilter(watched, event);

        auto *key = static_cast<QKeyEvent *>(event);
        if (isRemoteOk(key)) {
            QKeyEvent ret(key->type(), Qt::Key_Return, key->modifiers(), key->nativeScanCode(),
                          key->nativeVirtualKey(), key->nativeModifiers(), QString(),
                          key->isAutoRepeat(), key->count());
            QCoreApplication::sendEvent(watched, &ret);
            return true;
        }
        if (event->type() == QEvent::KeyPress
            && (key->key() == 0 || key->key() == Qt::Key_unknown)) {
            qCDebug(logApp) << "input: unrecognised key, scan code" << key->nativeScanCode()
                            << "keysym" << Qt::hex << key->nativeVirtualKey();
        }
        return QObject::eventFilter(watched, event);
    }
};

} // namespace strmqt
