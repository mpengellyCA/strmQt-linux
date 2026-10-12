#pragma once

#include <QCoreApplication>
#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QTimer>
#include <QWindow>

// A mouse nobody is holding is not an input device, it is a rectangle the
// pointer happens to be parked over.
//
// Hover is drawn by where the pointer IS, and Qt Quick re-evaluates it every
// frame the scene moves — so a pointer left on the screen while the user
// picked up a pad kept lighting cards as rows scrolled beneath it, raising
// tooltips, opening the destination rail, and showing a second highlight
// beside the focus ring. The player page already hid the pointer after a few
// idle seconds; this is that rule for the whole window.
//
// After `idleMs` without the pointer moving, a button or the wheel:
//  - the cursor is blanked (an application override, so it wins over every
//    control's own cursorShape), and
//  - the window is told the pointer left. That is Qt's own way of saying
//    "nothing is hovered": every HoverHandler lets go, and with no last
//    position to re-test, a scene that scrolls afterwards hovers nothing.
// The first real movement puts both back; Qt re-hovers from the move itself.
//
// Parking is not taken while a button is down — a held scrubber is in use —
// and waking takes a few pixels of travel, because a mouse at rest on a desk
// still reports the odd count.
namespace strmqt {

class PointerIdle final : public QObject
{
    Q_OBJECT

public:
    explicit PointerIdle(QWindow *window, QObject *parent = nullptr)
        : QObject(parent), m_window(window)
    {
        m_timer.setSingleShot(true);
        m_timer.setInterval(5000);
        connect(&m_timer, &QTimer::timeout, this, &PointerIdle::park);
        // On the application, not the window: the blanked cursor is an
        // application override, so movement over ANY of our windows has to be
        // able to bring it back — a dialog must not open with no pointer.
        qApp->installEventFilter(this);
    }

    ~PointerIdle() override { setParked(false); }

    bool parked() const { return m_parked; }
    void setIdleMs(int ms) { m_timer.setInterval(ms); }

signals:
    void parkedChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (!watched->isWindowType())
            return QObject::eventFilter(watched, event);
        const bool mine = watched == m_window;
        switch (event->type()) {
        case QEvent::MouseMove: {
            const QPointF at = static_cast<QMouseEvent *>(event)->globalPosition();
            if (m_parked) {
                // Travel from where it was left, so drift adds up to nothing.
                if ((at - m_parkedAt).manhattanLength() < kWakeTravel)
                    return true; // not a movement; nothing should hover from it
                setParked(false);
            }
            m_lastAt = at;
            m_timer.start();
            break;
        }
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
            m_buttonsDown = true;
            setParked(false);
            m_timer.stop();
            break;
        case QEvent::MouseButtonRelease:
            m_buttonsDown = static_cast<QMouseEvent *>(event)->buttons() != Qt::NoButton;
            if (!m_buttonsDown)
                m_timer.start();
            break;
        case QEvent::Wheel:
        case QEvent::TabletMove:
        case QEvent::TabletPress:
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
            setParked(false);
            m_timer.start();
            break;
        case QEvent::Enter:
            // The pointer came in from outside: that is movement, and the
            // clock starts from it.
            if (mine && !m_leaving) {
                setParked(false);
                m_timer.start();
            }
            break;
        case QEvent::Leave:
            if (mine && !m_leaving)
                m_timer.stop(); // it is over another window; nothing to hide
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    static constexpr int kWakeTravel = 4;

    void park()
    {
        if (m_buttonsDown || m_parked || !m_window)
            return;
        m_parkedAt = m_lastAt;
        setParked(true);
    }

    void setParked(bool parked)
    {
        if (m_parked == parked)
            return;
        m_parked = parked;
        if (parked) {
            QGuiApplication::setOverrideCursor(QCursor(Qt::BlankCursor));
            if (m_window) {
                m_leaving = true;
                QEvent leave(QEvent::Leave);
                QCoreApplication::sendEvent(m_window, &leave);
                m_leaving = false;
            }
        } else {
            QGuiApplication::restoreOverrideCursor();
        }
        emit parkedChanged();
    }

    QPointer<QWindow> m_window;
    QTimer m_timer;
    QPointF m_lastAt;
    QPointF m_parkedAt;
    bool m_parked = false;
    bool m_buttonsDown = false;
    bool m_leaving = false;
};

} // namespace strmqt
