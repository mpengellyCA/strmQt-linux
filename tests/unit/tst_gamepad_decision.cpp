#include <QtTest>

#include "input/GamepadDecision.h"

using namespace strmqt;

// There is no gamepad on a build machine, so the rules GamepadManager applies to
// a synthesized keystroke are only checkable if they are pure. These are the two
// that were wrong: a held stick scrubbing the player at 26 steps a second, and a
// pad button bound to a letter being handed to a focused text field.
class GamepadDecisionTest : public QObject
{
    Q_OBJECT

private slots:
    void browseRepeatAccelerates();
    void playerSeekIsClampedToAUsableScrub();
    void onlyHorizontalPlayerDirectionsCountAsSeeking();
    void typableKeysAreTheOnesAFieldWouldEat();
    void suppressionOnlyAppliesWhileAFieldHasFocus();
    void disconnectReleasesOnlyItsDevice();
    void pollCadenceSleepsWhileWaitingForADevice();
    void aTapSelectsAndAHoldOpensTheItemMenu();
    void aHoldThatFiredDoesNotAlsoSelectOnRelease();
    void aCancelledPressMeansNeither();
};

void GamepadDecisionTest::browseRepeatAccelerates()
{
    const RepeatTuning tuning;
    // Crossing a 1300-item grid is the case this ladder exists for: six steps at
    // the first rate, then the fast one for as long as it is held.
    QCOMPARE(repeatIntervalMs(2, false, tuning), tuning.fastMs);
    QCOMPARE(repeatIntervalMs(7, false, tuning), tuning.fastMs);
    QCOMPARE(repeatIntervalMs(8, false, tuning), tuning.fastestMs);
    QCOMPARE(repeatIntervalMs(400, false, tuning), tuning.fastestMs);
}

void GamepadDecisionTest::playerSeekIsClampedToAUsableScrub()
{
    const RepeatTuning tuning;
    // Every step is a 10 s seek and a server round-trip. At the grid's floor a
    // held stick asks for over four minutes of media a second.
    QVERIFY(tuning.seekFloorMs > tuning.fastestMs);
    QCOMPARE(repeatIntervalMs(2, true, tuning), tuning.seekFloorMs);
    QCOMPARE(repeatIntervalMs(8, true, tuning), tuning.seekFloorMs);
    QCOMPARE(repeatIntervalMs(400, true, tuning), tuning.seekFloorMs);

    // A scrub the user can stop on a scene: roughly 40 s of media per real
    // second, an hour of runtime in a minute and a half of holding.
    const double mediaPerSecond = 10.0 * 1000.0 / tuning.seekFloorMs;
    QVERIFY(mediaPerSecond > 20.0 && mediaPerSecond < 60.0);
}

void GamepadDecisionTest::onlyHorizontalPlayerDirectionsCountAsSeeking()
{
    const QString player = QStringLiteral("player");
    const QString browse = QStringLiteral("browse");
    QVERIFY(isSeekRepeat(player, QStringLiteral("nav.left")));
    QVERIFY(isSeekRepeat(player, QStringLiteral("nav.right")));
    // Up/Down are volume, and the OSD's own lists still want the fast ladder.
    QVERIFY(!isSeekRepeat(player, QStringLiteral("nav.up")));
    QVERIFY(!isSeekRepeat(player, QStringLiteral("nav.down")));
    // Browsing is untouched: this clamp must not slow a 1300-item grid down.
    QVERIFY(!isSeekRepeat(browse, QStringLiteral("nav.left")));
    QVERIFY(!isSeekRepeat(browse, QStringLiteral("nav.right")));
}

void GamepadDecisionTest::typableKeysAreTheOnesAFieldWouldEat()
{
    // The bindings the pad actually resolves to: Menu → "M", Y → "/".
    QVERIFY(isTypableKey(Qt::Key_M, Qt::NoModifier));
    QVERIFY(isTypableKey(Qt::Key_Slash, Qt::NoModifier));
    QVERIFY(isTypableKey(Qt::Key_Space, Qt::NoModifier));
    // "?" is Shift+/ and is as typable as anything else.
    QVERIFY(isTypableKey(Qt::Key_Question, Qt::ShiftModifier));

    // Navigation must reach the field, or a search box cannot be left with the
    // pad at all.
    QVERIFY(!isTypableKey(Qt::Key_Left, Qt::NoModifier));
    QVERIFY(!isTypableKey(Qt::Key_Right, Qt::NoModifier));
    QVERIFY(!isTypableKey(Qt::Key_Up, Qt::NoModifier));
    QVERIFY(!isTypableKey(Qt::Key_Down, Qt::NoModifier));
    QVERIFY(!isTypableKey(Qt::Key_Escape, Qt::NoModifier));
    QVERIFY(!isTypableKey(Qt::Key_Return, Qt::NoModifier));
    QVERIFY(!isTypableKey(Qt::Key_Enter, Qt::NoModifier));
    QVERIFY(!isTypableKey(Qt::Key_Tab, Qt::NoModifier));
    QVERIFY(!isTypableKey(Qt::Key_Backspace, Qt::NoModifier));
    QVERIFY(!isTypableKey(Qt::Key_PageUp, Qt::NoModifier));
    QVERIFY(!isTypableKey(Qt::Key_PageDown, Qt::NoModifier));
    QVERIFY(!isTypableKey(Qt::Key_F2, Qt::NoModifier));

    // A chord is never typing: Ctrl+K opens the command palette from inside the
    // search box, which is exactly where a user reaches for it.
    QVERIFY(!isTypableKey(Qt::Key_K, Qt::ControlModifier));
    QVERIFY(!isTypableKey(Qt::Key_Tab, Qt::ControlModifier | Qt::ShiftModifier));
}

void GamepadDecisionTest::suppressionOnlyAppliesWhileAFieldHasFocus()
{
    // Nothing is suppressed when no field has focus — the pad's letters are how
    // the menu and the search page are opened in the first place.
    QVERIFY(!shouldSuppressKey(Qt::Key_M, Qt::NoModifier, false));
    QVERIFY(shouldSuppressKey(Qt::Key_M, Qt::NoModifier, true));
    QVERIFY(!shouldSuppressKey(Qt::Key_Escape, Qt::NoModifier, true));
    QVERIFY(!shouldSuppressKey(Qt::Key_K, Qt::ControlModifier, true));
}

void GamepadDecisionTest::disconnectReleasesOnlyItsDevice()
{
    HeldKeyOwnership ownership;
    constexpr quint64 left = 0x100;
    constexpr quint64 right = 0x101;

    QVERIFY(ownership.press(11, left));      // first owner: send Qt key down
    QVERIFY(!ownership.press(11, left));     // stick + D-pad on one device
    QVERIFY(!ownership.press(22, left));    // same key remains one Qt hold
    QVERIFY(ownership.press(22, right));
    QCOMPARE(ownership.owners(left), 3);

    const QList<quint64> released = ownership.releaseDevice(11);
    QVERIFY(released.isEmpty());             // device 22 still holds Left
    QCOMPARE(ownership.owners(left), 1);
    QVERIFY(!ownership.release(11, left));   // duplicate SDL up is harmless

    const QList<quint64> final = ownership.releaseDevice(22);
    QVERIFY(final.contains(left));
    QVERIFY(final.contains(right));
    QCOMPARE(ownership.owners(left), 0);
}

void GamepadDecisionTest::pollCadenceSleepsWhileWaitingForADevice()
{
    const int idle = gamepadPollIntervalMs(false);
    const int active = gamepadPollIntervalMs(true);

    QCOMPARE(active, 16);
    QVERIFY(idle >= 250);
    QVERIFY(idle > active * 10);
}

// A is the one button in browse context with two meanings, and the whole of
// telling them apart is an ordering: which of the release and the threshold
// arrives first. A device cannot be made to reproduce that on demand, which is
// why the rule is a pure state machine and this test exists.
void GamepadDecisionTest::aTapSelectsAndAHoldOpensTheItemMenu()
{
    SelectHold tap;
    tap.press();
    // Every poll before the threshold says nothing at all: a select that fired
    // early would open the item under the cursor half way through a hold.
    QCOMPARE(tap.tick(0), SelectGesture::None);
    QCOMPARE(tap.tick(kSelectHoldMs - 1), SelectGesture::None);
    QCOMPARE(tap.release(), SelectGesture::Select);

    SelectHold hold;
    hold.press();
    QCOMPARE(hold.tick(kSelectHoldMs), SelectGesture::ContextMenu);
}

void GamepadDecisionTest::aHoldThatFiredDoesNotAlsoSelectOnRelease()
{
    SelectHold hold;
    hold.press();
    QCOMPARE(hold.tick(kSelectHoldMs + 200), SelectGesture::ContextMenu);
    // Polling continues while the button is still down; the menu opens once.
    QCOMPARE(hold.tick(kSelectHoldMs + 400), SelectGesture::None);
    // And letting go must not activate whatever the menu opened over.
    QCOMPARE(hold.release(), SelectGesture::None);
    QVERIFY(!hold.pending());
}

void GamepadDecisionTest::aCancelledPressMeansNeither()
{
    // A context switch or a disconnect mid-press. The press is forgotten, so
    // the release that eventually arrives selects nothing on the new surface.
    SelectHold interrupted;
    interrupted.press();
    interrupted.cancel();
    QCOMPARE(interrupted.tick(kSelectHoldMs * 10), SelectGesture::None);
    QCOMPARE(interrupted.release(), SelectGesture::None);

    // A release with no press behind it (the button was down before the context
    // became one that has this gesture) is equally a no-op.
    SelectHold stray;
    QCOMPARE(stray.release(), SelectGesture::None);
}

QTEST_MAIN(GamepadDecisionTest)
#include "tst_gamepad_decision.moc"
