// A remote's ⏭ / ⏮ caught at the window (src/input/SkipKeyFilter.h): bound
// through the input map, consumed only while something plays, and answering
// the two action ids for the inputs that have no key.

#include "input/InputMap.h"
#include "input/SkipKeyFilter.h"

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

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        pressed << event->key();
        event->accept();
    }
};

} // namespace

class TestSkipKeyFilter : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QVERIFY(m_dir.isValid());
        m_input = new strmqt::InputMap(m_dir.filePath(QStringLiteral("input.ini")));
        m_live = true;
        m_calls.clear();
        m_filter = new strmqt::SkipKeyFilter(
            m_input,
            {[this] { return m_live; }, [this] { m_calls << QStringLiteral("forward"); },
             [this] { m_calls << QStringLiteral("back"); }});
        m_window = new QQuickWindow;
        m_window->installEventFilter(m_filter);
        m_item = new KeyRecorder;
        m_item->setParentItem(m_window->contentItem());
        m_item->setFocus(true);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
    }

    void cleanup()
    {
        delete m_window;
        m_window = nullptr;
        delete m_filter;
        m_filter = nullptr;
        delete m_input;
        m_input = nullptr;
    }

    void mediaKeysSkipAndNeverReachThePage()
    {
        press(Qt::Key_MediaNext);
        release(Qt::Key_MediaNext);
        press(Qt::Key_MediaPrevious);
        release(Qt::Key_MediaPrevious);
        QCOMPARE(m_calls, (QStringList{QStringLiteral("forward"), QStringLiteral("back")}));
        QVERIFY(m_item->pressed.isEmpty());
    }

    void autoRepeatDoesNotRaceThroughChapters()
    {
        press(Qt::Key_MediaNext);
        press(Qt::Key_MediaNext, true);
        press(Qt::Key_MediaNext, true);
        release(Qt::Key_MediaNext);
        QCOMPARE(m_calls, QStringList{QStringLiteral("forward")});
        QVERIFY(m_item->pressed.isEmpty());
    }

    void nothingPlayingLetsTheKeyThrough()
    {
        m_live = false;
        press(Qt::Key_MediaNext);
        QVERIFY(m_calls.isEmpty());
        QCOMPARE(m_item->pressed, QList<int>{Qt::Key_MediaNext});
    }

    void otherKeysPassThrough()
    {
        press(Qt::Key_Down);
        press(Qt::Key_AudioForward); // ⏩ is a seek, answered by the player page
        QVERIFY(m_calls.isEmpty());
        QCOMPARE(m_item->pressed, (QList<int>{Qt::Key_Down, Qt::Key_AudioForward}));
    }

    void followsTheBinding()
    {
        QVERIFY(m_input->setBinding(QStringLiteral("player.skipForward"), QStringLiteral("F8")));
        press(Qt::Key_MediaNext);
        press(Qt::Key_F8);
        QCOMPARE(m_calls, QStringList{QStringLiteral("forward")});
        QCOMPARE(m_item->pressed, QList<int>{Qt::Key_MediaNext});
    }

    void actionIdsAreTaps()
    {
        QVERIFY(m_filter->invokeAction(QStringLiteral("player.skipForward"), false));
        QVERIFY(m_filter->invokeAction(QStringLiteral("player.skipBack"), false));
        // A repeat is answered and spent.
        QVERIFY(m_filter->invokeAction(QStringLiteral("player.skipForward"), true));
        QVERIFY(!m_filter->invokeAction(QStringLiteral("player.stop"), false));
        QCOMPARE(m_calls, (QStringList{QStringLiteral("forward"), QStringLiteral("back")}));

        m_live = false;
        QVERIFY(!m_filter->invokeAction(QStringLiteral("player.skipForward"), false));
    }

private:
    void press(int key, bool autoRepeat = false)
    {
        QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier, QString(), autoRepeat);
        QCoreApplication::sendEvent(m_window, &event);
    }
    void release(int key, bool autoRepeat = false)
    {
        QKeyEvent event(QEvent::KeyRelease, key, Qt::NoModifier, QString(), autoRepeat);
        QCoreApplication::sendEvent(m_window, &event);
    }

    QTemporaryDir m_dir;
    strmqt::InputMap *m_input = nullptr;
    strmqt::SkipKeyFilter *m_filter = nullptr;
    QQuickWindow *m_window = nullptr;
    KeyRecorder *m_item = nullptr;
    bool m_live = true;
    QStringList m_calls;
};

QTEST_MAIN(TestSkipKeyFilter)
#include "tst_skip_key_filter.moc"
