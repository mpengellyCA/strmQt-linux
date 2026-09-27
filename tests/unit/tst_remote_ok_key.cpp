// Does a TV remote's OK button reach the focused item as Return?
//
// Bluetooth remotes in D-pad mode send KEY_SELECT (Qt::Key_Select) or KEY_OK
// (key 0, native keysym XF86OK) for OK, and the app only answers Return. The
// filter (src/input/RemoteOkKeyFilter.h) rewrites both; everything else passes
// through untouched.

#include "input/RemoteOkKeyFilter.h"

#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

namespace {

class KeyRecorder : public QQuickItem
{
public:
    QList<int> pressed;
    QList<int> released;

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        pressed << event->key();
        event->accept();
    }
    void keyReleaseEvent(QKeyEvent *event) override
    {
        released << event->key();
        event->accept();
    }
};

} // namespace

class TestRemoteOkKey : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_window = new QQuickWindow;
        m_window->installEventFilter(new strmqt::RemoteOkKeyFilter(m_window));
        m_item = new KeyRecorder;
        m_item->setParentItem(m_window->contentItem());
        m_item->setFocus(true);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
        QVERIFY(m_item->hasActiveFocus());
    }

    void cleanup()
    {
        delete m_window;
        m_window = nullptr;
    }

    void keySelectBecomesReturn()
    {
        send(Qt::Key_Select, 0);
        QCOMPARE(m_item->pressed, QList<int>{Qt::Key_Return});
        QCOMPARE(m_item->released, QList<int>{Qt::Key_Return});
    }

    void xf86OkBecomesReturn()
    {
        send(0, strmqt::RemoteOkKeyFilter::kXF86OkKeysym);
        QCOMPARE(m_item->pressed, QList<int>{Qt::Key_Return});
        QCOMPARE(m_item->released, QList<int>{Qt::Key_Return});
    }

    void otherKeysPassThrough()
    {
        send(Qt::Key_Down, 0);
        send(Qt::Key_Return, 0);
        send(0, 0x10081162); // some other unnamed remote key stays unnamed
        QCOMPARE(m_item->pressed, (QList<int>{Qt::Key_Down, Qt::Key_Return, 0}));
    }

private:
    void send(int key, quint32 keysym)
    {
        for (QEvent::Type type : {QEvent::KeyPress, QEvent::KeyRelease}) {
            QKeyEvent event(type, key, Qt::NoModifier, 0, keysym, 0);
            QCoreApplication::sendEvent(m_window, &event);
        }
    }

    QQuickWindow *m_window = nullptr;
    KeyRecorder *m_item = nullptr;
};

QTEST_MAIN(TestRemoteOkKey)
#include "tst_remote_ok_key.moc"
