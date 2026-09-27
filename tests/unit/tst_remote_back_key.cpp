// Does a TV remote's Back button reach the focused item as Esc?
//
// Remotes send KEY_BACK (Qt::Key_Back) for Back. Every panel, menu and dialog in
// the app answers Esc by name, so the filter (src/input/RemoteBackKeyFilter.h)
// delivers nav.back's primary key instead — and follows a rebind, while a
// typable key bound to nav.back (Backspace) is never rewritten.

#include "input/InputMap.h"
#include "input/RemoteBackKeyFilter.h"

#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
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

class TestRemoteBackKey : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QVERIFY(m_dir.isValid());
        m_input = new strmqt::InputMap(m_dir.filePath(QStringLiteral("input.ini")));
        m_window = new QQuickWindow;
        m_window->installEventFilter(new strmqt::RemoteBackKeyFilter(m_input, m_window));
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
        delete m_input;
        m_input = nullptr;
    }

    void backBecomesEscape()
    {
        send(Qt::Key_Back);
        QCOMPARE(m_item->pressed, QList<int>{Qt::Key_Escape});
        QCOMPARE(m_item->released, QList<int>{Qt::Key_Escape});
    }

    void escapeAndTypableBindingsPassThrough()
    {
        // Esc is already the target; Backspace is bound to nav.back too, but a
        // search box needs it to delete, so it is never rewritten.
        send(Qt::Key_Escape);
        send(Qt::Key_Backspace);
        send(Qt::Key_Down);
        QCOMPARE(m_item->pressed,
                 (QList<int>{Qt::Key_Escape, Qt::Key_Backspace, Qt::Key_Down}));
    }

    void followsTheBinding()
    {
        // Unbound from nav.back, Back is just a key again.
        QVERIFY(m_input->setBindings(QStringLiteral("nav.back"),
                                     {QStringLiteral("Esc"), QStringLiteral("Backspace")}));
        send(Qt::Key_Back);
        QCOMPARE(m_item->pressed, QList<int>{Qt::Key_Back});

        // A different primary is what Back — and the pad's B — then deliver.
        QVERIFY(m_input->setBindings(QStringLiteral("nav.back"),
                                     {QStringLiteral("F9"), QStringLiteral("Back")}));
        m_item->pressed.clear();
        send(Qt::Key_Back);
        QCOMPARE(m_item->pressed, QList<int>{Qt::Key_F9});
    }

private:
    void send(int key)
    {
        for (QEvent::Type type : {QEvent::KeyPress, QEvent::KeyRelease}) {
            QKeyEvent event(type, key, Qt::NoModifier);
            QCoreApplication::sendEvent(m_window, &event);
        }
    }

    QTemporaryDir m_dir;
    strmqt::InputMap *m_input = nullptr;
    QQuickWindow *m_window = nullptr;
    KeyRecorder *m_item = nullptr;
};

QTEST_MAIN(TestRemoteBackKey)
#include "tst_remote_back_key.moc"
