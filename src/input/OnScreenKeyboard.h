#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QVariantList>

class QKeyEvent;
class QWindow;

// A keyboard drawn on screen, for a text field reached with a pad, a TV remote
// or the phone remote — none of which can type.
//
// The field keeps the focus the whole time. The keyboard is not a surface the
// focus moves into: while it is open this filter, on the window, takes the
// arrows, Select and Back before the field sees them and uses them to walk and
// press the keys drawn on screen, and a pressed key reaches the field as an
// ordinary key event with text. So the field cannot tell it from typing:
// textEdited, accepted, a validator, a page's search-as-you-type and a popup
// that closes when its field loses focus all behave as they do for a keyboard,
// and a field inside a popup is not pulled out of it.
//
// It opens on Select pressed in a field by something that is not a keyboard: a
// key the pad or the phone delivered (keydelivery::DeliveredKey), or a remote's
// OK rewritten to Return (RemoteOkKeyFilter). What those have in common is that
// the window system did not send them, so the test is QEvent::spontaneous().
// Return from a real keyboard still submits the field, and once the keyboard is
// open the first character typed on a real keyboard closes it and is typed.
//
// A field opts out by declaring `property bool onScreenKeyboard: false` on
// itself or on any item around it — the way FocusNavigator reads nav*: by name,
// so no QML imports anything. YouTube's TV interface does, because it draws a
// keyboard of its own.
//
// The layout and the cursor's walk over it are plain functions (layoutRows,
// step) so they can be tested without a window; QML only draws `rows` and
// reports a click as press(row, column).
namespace strmqt {

class OnScreenKeyboard final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool open READ isOpen NOTIFY openChanged)
    // Rows of keys, each a map: kind (char, shift, symbols, space, backspace,
    // left, right, done), text (what a char key types, shift applied) and width
    // (in letter keys; every row adds up to kRowWidth).
    Q_PROPERTY(QVariantList rows READ rows NOTIFY layoutChanged)
    Q_PROPERTY(bool shifted READ shifted NOTIFY layoutChanged)
    Q_PROPERTY(bool symbols READ symbols NOTIFY layoutChanged)
    Q_PROPERTY(int row READ row NOTIFY cursorChanged)
    Q_PROPERTY(int column READ column NOTIFY cursorChanged)
    // The field's text as it may be shown: bullets for a password.
    Q_PROPERTY(QString text READ text NOTIFY textChanged)
    Q_PROPERTY(bool secret READ secret NOTIFY textChanged)

public:
    enum class Kind { Char, Shift, Symbols, Space, Backspace, Left, Right, Done };

    struct Key
    {
        Kind kind = Kind::Char;
        QString text;
        double width = 1.0;
    };
    using Layout = QList<QList<Key>>;

    static constexpr double kRowWidth = 10.0;

    // Where the cursor is, and the x (in letter keys, from the row's left edge)
    // a vertical step aims for. The x is the key's centre after a horizontal
    // step and is left alone by a vertical one, so Down off a narrow key onto
    // the space bar and Up again lands back in the column it left.
    struct Cursor
    {
        int row = 0;
        int column = 0;
        double x = 0.5;

        bool operator==(const Cursor &other) const
        {
            return row == other.row && column == other.column;
        }
    };

    explicit OnScreenKeyboard(QObject *parent = nullptr);

    // Starts watching the window's keys and focus. Once, after the QML loads.
    void attach(QWindow *window);

    static Layout layoutRows(bool symbols, bool shifted);
    // One step of the cursor. The edges are edges: a step off any side of the
    // keyboard stays where it is.
    static Cursor step(const Layout &layout, Cursor from, int dx, int dy);
    // The key a character is typed as. Qt names every printable ASCII
    // character by its upper-case code point.
    static int qtKeyFor(QChar character);

    bool isOpen() const { return m_open; }
    QVariantList rows() const;
    bool shifted() const { return m_shifted; }
    bool symbols() const { return m_symbols; }
    int row() const { return m_cursor.row; }
    int column() const { return m_cursor.column; }
    QString text() const { return m_text; }
    bool secret() const { return m_secret; }

    // A key pressed with the pointer; the cursor moves there too.
    Q_INVOKABLE void press(int row, int column);
    // Hides the keyboard and leaves the field focused, with what was typed.
    Q_INVOKABLE void close();

signals:
    void openChanged();
    void layoutChanged();
    void cursorChanged();
    void textChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool handleClosed(QKeyEvent *key);
    bool handleOpen(QKeyEvent *key);
    bool wantsKeyboard(QObject *focus) const;
    void openFor(QObject *focus);
    void activate(const Key &key);
    void moveCursor(int dx, int dy);
    void setLayer(bool symbols, bool shifted);
    void sendToField(int key, Qt::KeyboardModifiers modifiers, const QString &text);
    void readField();

    QPointer<QWindow> m_window;
    QPointer<QObject> m_field;
    Layout m_layout;
    Cursor m_cursor;
    // Presses this filter took; their releases are taken too, so nothing under
    // the keyboard sees half a keystroke.
    QSet<int> m_taken;
    QString m_text;
    bool m_open = false;
    bool m_shifted = false;
    bool m_symbols = false;
    bool m_secret = false;
    bool m_sending = false;
};

} // namespace strmqt
