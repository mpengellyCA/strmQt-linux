// Does a window in the background keep its focused item for posted keys?
//
// The web remote and the gamepad post key events to the app window without it
// being active. Qt clears a QQuickWindow's focus chain when another window is
// activated, so those keys went nowhere until something re-seated focus. Both
// halves are measured: the unfiltered window is the bug, the filtered one is
// the fix (src/app/WindowFocusKeeper.h).

#include "WindowFocusKeeper.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QWindow>

namespace {

class KeyCounter : public QQuickItem
{
public:
    int presses = 0;

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        ++presses;
        event->accept();
    }
};

struct Result
{
    // False when the platform would not move activation, so nothing was measured.
    bool ran = false;
    bool keptFocus = false;
    int presses = 0;
    bool focusedAfterReturn = false;
};

Result sendAwayAndPost(bool keep)
{
    QQuickWindow window;
    window.resize(200, 200);
    if (keep)
        window.installEventFilter(new strmqt::WindowFocusKeeper(&window));
    auto *item = new KeyCounter;
    item->setParentItem(window.contentItem());
    item->setFocus(true);
    window.show();
    window.requestActivate();
    if (!QTest::qWaitForWindowActive(&window) || !item->hasActiveFocus())
        return {};

    QWindow other;
    other.resize(100, 100);
    other.show();
    other.requestActivate();
    if (!QTest::qWaitForWindowActive(&other) || window.isActive())
        return {};

    Result result;
    result.ran = true;
    result.keptFocus = window.activeFocusItem() == item;
    for (int i = 0; i < 2; ++i) {
        QCoreApplication::postEvent(&window,
                                    new QKeyEvent(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier));
        QCoreApplication::postEvent(&window,
                                    new QKeyEvent(QEvent::KeyRelease, Qt::Key_Down, Qt::NoModifier));
    }
    QCoreApplication::processEvents();
    result.presses = item->presses;

    window.requestActivate();
    if (QTest::qWaitForWindowActive(&window))
        result.focusedAfterReturn = window.activeFocusItem() == item;
    return result;
}

} // namespace

class TestWindowFocusKeeper : public QObject
{
    Q_OBJECT

private slots:
    void unfilteredWindowDropsFocus()
    {
        const Result result = sendAwayAndPost(false);
        QVERIFY(result.ran);
        QVERIFY(!result.keptFocus);
        QCOMPARE(result.presses, 0);
    }

    void keeperHoldsFocusForPostedKeys()
    {
        const Result result = sendAwayAndPost(true);
        QVERIFY(result.ran);
        QVERIFY(result.keptFocus);
        QCOMPARE(result.presses, 2);
        QVERIFY(result.focusedAfterReturn);
    }
};

QTEST_MAIN(TestWindowFocusKeeper)
#include "tst_window_focus_keeper.moc"
