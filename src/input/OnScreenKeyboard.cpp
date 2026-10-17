#include "OnScreenKeyboard.h"

#include "input/KeyDelivery.h"

#include <QCoreApplication>
#include <QInputMethodQueryEvent>
#include <QKeyEvent>
#include <QQuickItem>
#include <QVariantMap>
#include <QWindow>

#include <cmath>
#include <limits>

namespace strmqt {

namespace {

QList<OnScreenKeyboard::Key> characters(const QString &keys, bool shifted)
{
    QList<OnScreenKeyboard::Key> row;
    for (const QChar c : keys)
        row.append({OnScreenKeyboard::Kind::Char, shifted ? QString(c.toUpper()) : QString(c), 1.0});
    return row;
}

double centreOf(const QList<OnScreenKeyboard::Key> &keys, int column)
{
    double left = 0.0;
    for (int i = 0; i < column; ++i)
        left += keys.at(i).width;
    return left + keys.at(column).width / 2.0;
}

QString kindName(OnScreenKeyboard::Kind kind)
{
    switch (kind) {
    case OnScreenKeyboard::Kind::Char:      return QStringLiteral("char");
    case OnScreenKeyboard::Kind::Shift:     return QStringLiteral("shift");
    case OnScreenKeyboard::Kind::Symbols:   return QStringLiteral("symbols");
    case OnScreenKeyboard::Kind::Space:     return QStringLiteral("space");
    case OnScreenKeyboard::Kind::Backspace: return QStringLiteral("backspace");
    case OnScreenKeyboard::Kind::Left:      return QStringLiteral("left");
    case OnScreenKeyboard::Kind::Right:     return QStringLiteral("right");
    case OnScreenKeyboard::Kind::Done:      return QStringLiteral("done");
    }
    return QString();
}

} // namespace

OnScreenKeyboard::OnScreenKeyboard(QObject *parent)
    : QObject(parent)
    , m_layout(layoutRows(false, false))
    // The first letter, where a TV keyboard starts.
    , m_cursor{1, 0, 0.5}
{
}

void OnScreenKeyboard::attach(QWindow *window)
{
    if (!window || m_window)
        return;
    m_window = window;
    window->installEventFilter(this);
    // Anything that takes the focus from the field — a click elsewhere, a page
    // that changed under it — ends the typing.
    connect(window, &QWindow::focusObjectChanged, this, [this](QObject *object) {
        if (m_open && object != m_field)
            close();
    });
}

// Every printable ASCII character is on one of the two layers, because a server
// address, a user name and a password can each need any of them. The letter
// layer also carries @ . - so an address or a host name rarely needs the other.
OnScreenKeyboard::Layout OnScreenKeyboard::layoutRows(bool symbols, bool shifted)
{
    Layout layout;
    layout.append(characters(QStringLiteral("1234567890"), false));
    if (symbols) {
        layout.append(characters(QStringLiteral("!?/:;'\"()&"), false));
        layout.append(characters(QStringLiteral("_#$%+=*,~\\"), false));
        layout.append(characters(QStringLiteral("<>[]{}|^`@"), false));
    } else {
        layout.append(characters(QStringLiteral("qwertyuiop"), shifted));
        layout.append(characters(QStringLiteral("asdfghjkl@"), shifted));
        QList<Key> row{{Kind::Shift, QString(), 1.0}};
        row.append(characters(QStringLiteral("zxcvbnm.-"), shifted));
        layout.append(row);
    }
    layout.append({{Kind::Symbols, QString(), 1.5},
                   {Kind::Left, QString(), 1.0},
                   {Kind::Space, QStringLiteral(" "), 3.5},
                   {Kind::Right, QString(), 1.0},
                   {Kind::Backspace, QString(), 1.5},
                   {Kind::Done, QString(), 1.5}});
    return layout;
}

OnScreenKeyboard::Cursor OnScreenKeyboard::step(const Layout &layout, Cursor from, int dx, int dy)
{
    if (from.row < 0 || from.row >= layout.size() || from.column < 0
        || from.column >= layout.at(from.row).size())
        return from;

    if (dx != 0) {
        const QList<Key> &keys = layout.at(from.row);
        const int column = from.column + (dx > 0 ? 1 : -1);
        if (column < 0 || column >= keys.size())
            return from;
        return {from.row, column, centreOf(keys, column)};
    }
    if (dy != 0) {
        const int row = from.row + (dy > 0 ? 1 : -1);
        if (row < 0 || row >= layout.size() || layout.at(row).isEmpty())
            return from;
        // The key under the remembered x, or the nearest one when the row is
        // shorter than that.
        const QList<Key> &keys = layout.at(row);
        double left = 0.0;
        int nearest = 0;
        double nearestDistance = std::numeric_limits<double>::max();
        for (int i = 0; i < keys.size(); ++i) {
            const double right = left + keys.at(i).width;
            if (from.x >= left && from.x < right)
                return {row, i, from.x};
            const double distance = std::abs((left + right) / 2.0 - from.x);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearest = i;
            }
            left = right;
        }
        return {row, nearest, from.x};
    }
    return from;
}

int OnScreenKeyboard::qtKeyFor(QChar character)
{
    const char16_t code = character.unicode();
    if (code >= 0x20 && code < 0x7f)
        return QChar(code).toUpper().unicode();
    return Qt::Key_unknown;
}

QVariantList OnScreenKeyboard::rows() const
{
    QVariantList rows;
    for (const QList<Key> &keys : m_layout) {
        QVariantList row;
        for (const Key &key : keys) {
            row.append(QVariantMap{{QStringLiteral("kind"), kindName(key.kind)},
                                   {QStringLiteral("text"), key.text},
                                   {QStringLiteral("width"), key.width}});
        }
        rows.append(QVariant(row));
    }
    return rows;
}

void OnScreenKeyboard::press(int row, int column)
{
    if (!m_open || row < 0 || row >= m_layout.size() || column < 0
        || column >= m_layout.at(row).size())
        return;
    m_cursor = {row, column, centreOf(m_layout.at(row), column)};
    emit cursorChanged();
    activate(m_layout.at(row).at(column));
}

void OnScreenKeyboard::close()
{
    if (!m_open)
        return;
    m_open = false;
    emit openChanged();
}

bool OnScreenKeyboard::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease)
        return QObject::eventFilter(watched, event);
    // The Return that Done hands the field goes through like any other key.
    if (m_sending)
        return QObject::eventFilter(watched, event);

    auto *key = static_cast<QKeyEvent *>(event);
    if (event->type() == QEvent::KeyRelease)
        return m_taken.remove(key->key()) || QObject::eventFilter(watched, event);
    return m_open ? handleOpen(key) : handleClosed(key);
}

bool OnScreenKeyboard::handleClosed(QKeyEvent *key)
{
    // A keyboard's Return submits the field, as it always has.
    if (key->spontaneous() || key->isAutoRepeat())
        return false;
    if (key->key() != Qt::Key_Return && key->key() != Qt::Key_Enter)
        return false;
    if (key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        return false;
    QObject *focus = m_window ? m_window->focusObject() : nullptr;
    if (!wantsKeyboard(focus))
        return false;
    m_taken.insert(key->key());
    openFor(focus);
    return true;
}

bool OnScreenKeyboard::handleOpen(QKeyEvent *key)
{
    const int code = key->key();
    switch (code) {
    case Qt::Key_Up:    moveCursor(0, -1); break;
    case Qt::Key_Down:  moveCursor(0, 1);  break;
    case Qt::Key_Left:  moveCursor(-1, 0); break;
    case Qt::Key_Right: moveCursor(1, 0);  break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (key->spontaneous()) {
            // A real keyboard's Return: the user is typing for themselves and
            // is submitting.
            close();
            return false;
        }
        activate(m_layout.at(m_cursor.row).at(m_cursor.column));
        break;
    case Qt::Key_Escape:
        close();
        break;
    default:
        if (key->spontaneous()) {
            // A real keyboard types for itself, and the drawn one gets out of
            // its way. Keys that edit nothing (media keys, a modifier going
            // down) leave it open.
            if (!key->text().isEmpty() || code == Qt::Key_Backspace || code == Qt::Key_Delete)
                close();
            return false;
        }
        // Anything else a pad or a remote sends — the context menu a held A
        // asks for, say — would land on the field or the page under the
        // keyboard, so it is taken and dropped.
        break;
    }
    m_taken.insert(code);
    return true;
}

bool OnScreenKeyboard::wantsKeyboard(QObject *focus) const
{
    if (!focus)
        return false;
    // A read-only field does not accept input methods, so this is also false
    // for one.
    QInputMethodQueryEvent query(Qt::ImEnabled);
    QCoreApplication::sendEvent(focus, &query);
    if (!query.value(Qt::ImEnabled).toBool())
        return false;
    for (auto *item = qobject_cast<QQuickItem *>(focus); item; item = item->parentItem()) {
        const QVariant optedIn = item->property("onScreenKeyboard");
        if (optedIn.isValid() && !optedIn.toBool())
            return false;
    }
    return true;
}

void OnScreenKeyboard::openFor(QObject *focus)
{
    m_field = focus;
    setLayer(false, false);
    readField();
    m_open = true;
    emit openChanged();
}

void OnScreenKeyboard::activate(const Key &key)
{
    switch (key.kind) {
    case Kind::Char:
        sendToField(qtKeyFor(key.text.at(0)),
                    key.text.at(0).isUpper() ? Qt::ShiftModifier : Qt::NoModifier, key.text);
        // Shift is for one letter, as on a phone.
        if (m_shifted)
            setLayer(m_symbols, false);
        break;
    case Kind::Shift:
        setLayer(m_symbols, !m_shifted);
        break;
    case Kind::Symbols:
        setLayer(!m_symbols, false);
        break;
    case Kind::Space:
        sendToField(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
        break;
    case Kind::Backspace:
        sendToField(Qt::Key_Backspace, Qt::NoModifier, QString());
        break;
    case Kind::Left:
        sendToField(Qt::Key_Left, Qt::NoModifier, QString());
        break;
    case Kind::Right:
        sendToField(Qt::Key_Right, Qt::NoModifier, QString());
        break;
    case Kind::Done: {
        // Return goes to the window, not straight to the field, so that it
        // carries on to whatever answers a Return the field declined — exactly
        // as a keyboard's would.
        QPointer<QWindow> window = m_window;
        close();
        if (window) {
            m_sending = true;
            keydelivery::DeliveredKey press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &press);
            keydelivery::DeliveredKey release(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &release);
            m_sending = false;
        }
        break;
    }
    }
}

void OnScreenKeyboard::moveCursor(int dx, int dy)
{
    const Cursor next = step(m_layout, m_cursor, dx, dy);
    const bool moved = !(next == m_cursor);
    m_cursor = next;
    if (moved)
        emit cursorChanged();
}

void OnScreenKeyboard::setLayer(bool symbols, bool shifted)
{
    if (symbols == m_symbols && shifted == m_shifted)
        return;
    m_symbols = symbols;
    m_shifted = shifted;
    m_layout = layoutRows(symbols, shifted);
    // Both layers have the same rows, but not the same keys in each.
    m_cursor.row = qBound(0, m_cursor.row, int(m_layout.size()) - 1);
    m_cursor.column = qBound(0, m_cursor.column, int(m_layout.at(m_cursor.row).size()) - 1);
    emit layoutChanged();
    emit cursorChanged();
}

// Straight to the field, not to the window: a key the field declines — Right
// at the end of the text, Backspace in an empty one — must not carry on to the
// page, where an arrow moves the focus away and Backspace is a Back binding.
void OnScreenKeyboard::sendToField(int key, Qt::KeyboardModifiers modifiers, const QString &text)
{
    if (!m_field)
        return;
    QKeyEvent press(QEvent::KeyPress, key, modifiers, text);
    QCoreApplication::sendEvent(m_field, &press);
    QKeyEvent release(QEvent::KeyRelease, key, modifiers, text);
    QCoreApplication::sendEvent(m_field, &release);
    readField();
}

void OnScreenKeyboard::readField()
{
    QString text;
    bool secret = false;
    if (m_field) {
        QInputMethodQueryEvent query(Qt::ImSurroundingText | Qt::ImHints);
        QCoreApplication::sendEvent(m_field, &query);
        text = query.value(Qt::ImSurroundingText).toString();
        const auto hints = static_cast<Qt::InputMethodHints>(query.value(Qt::ImHints).toInt());
        // A Password TextInput says SensitiveData, not HiddenText (Qt 6.12),
        // and hands over its real text all the same.
        secret = (hints & (Qt::ImhHiddenText | Qt::ImhSensitiveData)) != 0;
    }
    // A password is never handed to QML, only its length.
    if (secret)
        text = QString(text.size(), QChar(0x2022));
    if (text == m_text && secret == m_secret)
        return;
    m_text = text;
    m_secret = secret;
    emit textChanged();
}

} // namespace strmqt
