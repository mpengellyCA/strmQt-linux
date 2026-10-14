// Does a window in the background keep its focused item for posted keys?
//
// The web remote and the gamepad post key events to the app window without it
// being active. Qt clears a QQuickWindow's focus chain when another window is
// activated, so those keys went nowhere until something re-seated focus. Both
// halves are measured: the unfiltered window is the bug, the filtered one is
// the fix (src/app/WindowFocusKeeper.h).
//
// Keeping focus has a cost the same file pays back — a keyboard key held across
// the deactivation is released by the keeper, since nothing else will — and the
// keys that land in a background window are told apart from the keyboard's by
// DeliveredKeyStamp (src/input/KeyDelivery.h). Both are measured here too,
// because both need what this test already has: a real window with a focused
// item.

#include "WindowFocusKeeper.h"
#include "input/InputMap.h"
#include "input/KeyDelivery.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QWindow>

#include <functional>

namespace {

class KeyCounter : public QQuickItem
{
public:
    int presses = 0;
    QList<int> releases;
    // What a page's key handler does with a key it is handed.
    std::function<void()> onPress;

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        ++presses;
        if (onPress)
            onPress();
        event->accept();
    }
    void keyReleaseEvent(QKeyEvent *event) override
    {
        releases.append(event->key());
        event->accept();
    }
};

// A window with a focused item, active, optionally under the keeper.
struct Stage
{
    QQuickWindow window;
    KeyCounter *item = nullptr;

    explicit Stage(bool keep)
    {
        window.resize(200, 200);
        if (keep)
            window.installEventFilter(new strmqt::WindowFocusKeeper(&window));
        item = new KeyCounter;
        item->setParentItem(window.contentItem());
        item->setFocus(true);
        window.show();
        window.requestActivate();
    }

    bool ready() { return QTest::qWaitForWindowActive(&window) && item->hasActiveFocus(); }

    // What KWin sends when another application is clicked.
    void deactivate()
    {
        QFocusEvent focusOut(QEvent::FocusOut, Qt::OtherFocusReason);
        QCoreApplication::sendEvent(&window, &focusOut);
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

    // KWin on Wayland deactivates with Qt::OtherFocusReason rather than
    // Qt::ActiveWindowFocusReason; the offscreen platform cannot produce that,
    // so the event it sends is delivered by hand.
    void keeperIgnoresTheReasonTheCompositorGives()
    {
        for (const bool keep : {false, true}) {
            QQuickWindow window;
            window.resize(200, 200);
            if (keep)
                window.installEventFilter(new strmqt::WindowFocusKeeper(&window));
            auto *item = new KeyCounter;
            item->setParentItem(window.contentItem());
            item->setFocus(true);
            window.show();
            window.requestActivate();
            QVERIFY(QTest::qWaitForWindowActive(&window));
            QVERIFY(item->hasActiveFocus());

            QFocusEvent focusOut(QEvent::FocusOut, Qt::OtherFocusReason);
            QCoreApplication::sendEvent(&window, &focusOut);
            QCOMPARE(window.activeFocusItem() == item, keep);
        }
    }

    // The cost of keeping focus. The key's real release goes to whichever
    // application was clicked, and Qt never invents one: all an unfiltered
    // window does is drop focus. That was at least something a control could
    // end a hold on; with focus kept there is nothing, so the keeper releases
    // what the keyboard was holding.
    void qtItselfNeverReleasesAHeldKey()
    {
        Stage stage(false);
        QVERIFY(stage.ready());
        QTest::keyPress(&stage.window, Qt::Key_Right);
        QCOMPARE(stage.item->presses, 1);
        stage.deactivate();
        QVERIFY(stage.item->releases.isEmpty());
        QVERIFY(!stage.item->hasActiveFocus());
    }

    void keeperReleasesWhatTheKeyboardWasHolding()
    {
        Stage stage(true);
        QVERIFY(stage.ready());
        // One key pressed and let go normally, one still down.
        QTest::keyClick(&stage.window, Qt::Key_Up);
        QTest::keyPress(&stage.window, Qt::Key_Right);
        QCOMPARE(stage.item->releases, (QList<int>{Qt::Key_Up}));

        stage.deactivate();
        QCOMPARE(stage.item->releases, (QList<int>{Qt::Key_Up, Qt::Key_Right}));
        QVERIFY(stage.item->hasActiveFocus());

        // Once: the key is no longer held as far as anything here knows.
        stage.deactivate();
        QCOMPARE(stage.item->releases.size(), 2);
    }

    // The pad holds a direction down with a posted key, for as long as the
    // stick is held and whether or not the window is active. That hold is not
    // over when the window is deactivated, and the pad sends its own release.
    void keeperLeavesAKeyThePadIsHoldingAlone()
    {
        Stage stage(true);
        QVERIFY(stage.ready());
        strmqt::keydelivery::postKey(&stage.window, Qt::Key_Right, Qt::NoModifier, true, false);
        QCoreApplication::sendPostedEvents(&stage.window);
        QCOMPARE(stage.item->presses, 1);

        stage.deactivate();
        QVERIFY(stage.item->releases.isEmpty());

        strmqt::keydelivery::postKey(&stage.window, Qt::Key_Right, Qt::NoModifier, false, false);
        QCoreApplication::sendPostedEvents(&stage.window);
        QCOMPARE(stage.item->releases, (QList<int>{Qt::Key_Right}));
    }

    // The handlers a delivered key lands in stamp "keyboard"; the stamp is not
    // believed while one is being handled. A keystroke from the real keyboard
    // dispatched before the loop turns again must be believed all the same.
    void aRealKeystrokeEndsTheDeliveredKeyHold()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        strmqt::InputMap map(dir.filePath(QStringLiteral("input.ini")));
        Stage stage(true);
        stage.window.installEventFilter(
            new strmqt::keydelivery::DeliveredKeyStamp(&map, &stage.window));
        QVERIFY(stage.ready());
        stage.item->onPress = [&map] { map.noteInput(QStringLiteral("keyboard")); };

        map.noteInput(QStringLiteral("gamepad"));
        strmqt::keydelivery::postKey(&stage.window, Qt::Key_Down, Qt::NoModifier, true, false);
        // Only the window's events: the hold is let go by an event of its own,
        // and this is the turn of the loop in which it has not been yet.
        QCoreApplication::sendPostedEvents(&stage.window);
        QCOMPARE(stage.item->presses, 1);
        QCOMPARE(map.lastInputDevice(), QStringLiteral("gamepad"));

        // RemoteOkKeyFilter and RemoteBackKeyFilter answer a delivered key by
        // sending a plain QKeyEvent in its place. That is still the pad's.
        QKeyEvent rewritten(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QCoreApplication::sendEvent(&stage.window, &rewritten);
        QCOMPARE(stage.item->presses, 2);
        QCOMPARE(map.lastInputDevice(), QStringLiteral("gamepad"));

        QTest::keyPress(&stage.window, Qt::Key_Down);
        QCOMPARE(stage.item->presses, 3);
        QCOMPARE(map.lastInputDevice(), QStringLiteral("keyboard"));
    }

    // The other half of the same rule, unchanged: left to itself the hold lasts
    // exactly one turn of the loop.
    void theDeliveredKeyHoldLastsOneTurnOfTheLoop()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        strmqt::InputMap map(dir.filePath(QStringLiteral("input.ini")));
        Stage stage(true);
        stage.window.installEventFilter(
            new strmqt::keydelivery::DeliveredKeyStamp(&map, &stage.window));
        QVERIFY(stage.ready());

        map.noteInput(QStringLiteral("gamepad"));
        strmqt::keydelivery::postKey(&stage.window, Qt::Key_Down, Qt::NoModifier, true, false);
        QCoreApplication::sendPostedEvents(&stage.window);
        map.noteInput(QStringLiteral("keyboard"));
        QCOMPARE(map.lastInputDevice(), QStringLiteral("gamepad"));

        QCoreApplication::processEvents();
        map.noteInput(QStringLiteral("keyboard"));
        QCOMPARE(map.lastInputDevice(), QStringLiteral("keyboard"));
    }
};

QTEST_MAIN(TestWindowFocusKeeper)
#include "tst_window_focus_keeper.moc"
