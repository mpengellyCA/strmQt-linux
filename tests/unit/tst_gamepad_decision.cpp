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
    void heldVolumeIsASteadyRampNotALadder();
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
    QCOMPARE(repeatIntervalMs(2, RepeatPace::Ladder, tuning), tuning.fastMs);
    QCOMPARE(repeatIntervalMs(7, RepeatPace::Ladder, tuning), tuning.fastMs);
    QCOMPARE(repeatIntervalMs(8, RepeatPace::Ladder, tuning), tuning.fastestMs);
    QCOMPARE(repeatIntervalMs(400, RepeatPace::Ladder, tuning), tuning.fastestMs);
}

void GamepadDecisionTest::playerSeekIsClampedToAUsableScrub()
{
    const RepeatTuning tuning;
    // Every step is a 10 s seek and a server round-trip. At the grid's floor a
    // held stick asks for over four minutes of media a second.
    QVERIFY(tuning.seekFloorMs > tuning.fastestMs);
    QCOMPARE(repeatIntervalMs(2, RepeatPace::Seek, tuning), tuning.seekFloorMs);
    QCOMPARE(repeatIntervalMs(8, RepeatPace::Seek, tuning), tuning.seekFloorMs);
    QCOMPARE(repeatIntervalMs(400, RepeatPace::Seek, tuning), tuning.seekFloorMs);

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
    // Up/Down enter and leave the OSD, whose own lists still want the fast ladder.
    QVERIFY(!isSeekRepeat(player, QStringLiteral("nav.up")));
    QVERIFY(!isSeekRepeat(player, QStringLiteral("nav.down")));
    // Browsing is untouched: this clamp must not slow a 1300-item grid down.
    QVERIFY(!isSeekRepeat(browse, QStringLiteral("nav.left")));
    QVERIFY(!isSeekRepeat(browse, QStringLiteral("nav.right")));
}

void GamepadDecisionTest::heldVolumeIsASteadyRampNotALadder()
{
    const RepeatTuning tuning;
    const QString up = QStringLiteral("player.volumeUp");
    const QString down = QStringLiteral("player.volumeDown");
    QVERIFY(isVolumeRepeat(up));
    QVERIFY(isVolumeRepeat(down));
    QVERIFY(!isVolumeRepeat(QStringLiteral("nav.up")));
    QVERIFY(!isVolumeRepeat(QStringLiteral("player.seekForward")));

    // What GamepadManager asks on every step of a hold.
    QCOMPARE(repeatPace(false, up), RepeatPace::Volume);
    QCOMPARE(repeatPace(false, down), RepeatPace::Volume);
    QCOMPARE(repeatPace(false, QStringLiteral("nav.down")), RepeatPace::Ladder);
    QCOMPARE(repeatPace(true, QStringLiteral("nav.left")), RepeatPace::Seek);

    // No acceleration, however long it is held.
    for (const int emitted : {2, 7, 8, 9, 400})
        QCOMPARE(repeatIntervalMs(emitted, repeatPace(false, up), tuning), tuning.volumeMs);

    // Each step is 5% (PlayerPage adjustVolume), so the whole range is twenty
    // of them. From the first movement to the far end must take long enough to
    // let go in — on the ladder it was under a second and a half.
    // The first step is immediate, the second waits out the delay, and each one
    // after that waits the interval asked for once its predecessor was sent.
    int sweepMs = tuning.delayMs;
    for (int emitted = 2; emitted < 20; ++emitted)
        sweepMs += repeatIntervalMs(emitted, RepeatPace::Volume, tuning);
    QVERIFY2(sweepMs >= 2500, qPrintable(QString::number(sweepMs)));
    // And not so long that turning it down is a chore.
    QVERIFY2(sweepMs <= 4500, qPrintable(QString::number(sweepMs)));
}

void GamepadDecisionTest::typableKeysAreTheOnesAFieldWouldEat()
{
    // The bindings the pad actually resolves to: Menu → "M", Y → "/".
    QVERIFY(wouldTypeIntoField(Qt::Key_M, Qt::NoModifier));
    QVERIFY(wouldTypeIntoField(Qt::Key_Slash, Qt::NoModifier));
    QVERIFY(wouldTypeIntoField(Qt::Key_Space, Qt::NoModifier));
    // "?" is Shift+/ and is as typable as anything else.
    QVERIFY(wouldTypeIntoField(Qt::Key_Question, Qt::ShiftModifier));

    // Navigation must reach the field, or a search box cannot be left with the
    // pad at all.
    QVERIFY(!wouldTypeIntoField(Qt::Key_Left, Qt::NoModifier));
    QVERIFY(!wouldTypeIntoField(Qt::Key_Right, Qt::NoModifier));
    QVERIFY(!wouldTypeIntoField(Qt::Key_Up, Qt::NoModifier));
    QVERIFY(!wouldTypeIntoField(Qt::Key_Down, Qt::NoModifier));
    QVERIFY(!wouldTypeIntoField(Qt::Key_Escape, Qt::NoModifier));
    QVERIFY(!wouldTypeIntoField(Qt::Key_Return, Qt::NoModifier));
    QVERIFY(!wouldTypeIntoField(Qt::Key_Enter, Qt::NoModifier));
    QVERIFY(!wouldTypeIntoField(Qt::Key_Tab, Qt::NoModifier));
    QVERIFY(!wouldTypeIntoField(Qt::Key_Home, Qt::NoModifier));
    QVERIFY(!wouldTypeIntoField(Qt::Key_End, Qt::NoModifier));
    QVERIFY(!wouldTypeIntoField(Qt::Key_Backtab, Qt::ShiftModifier));
    QVERIFY(!wouldTypeIntoField(Qt::Key_PageUp, Qt::NoModifier));
    QVERIFY(!wouldTypeIntoField(Qt::Key_PageDown, Qt::NoModifier));
    QVERIFY(!wouldTypeIntoField(Qt::Key_F2, Qt::NoModifier));

    // The two keys that take a character out are typing, though they are not
    // characters: "Back" resolved to Backspace must not edit the search box.
    QVERIFY(wouldTypeIntoField(Qt::Key_Backspace, Qt::NoModifier));
    QVERIFY(wouldTypeIntoField(Qt::Key_Delete, Qt::NoModifier));
    QVERIFY(wouldTypeIntoField(Qt::Key_Backspace, Qt::ShiftModifier));
    // Ctrl+Backspace is a chord like any other.
    QVERIFY(!wouldTypeIntoField(Qt::Key_Backspace, Qt::ControlModifier));

    // A chord is never typing: Ctrl+K opens the command palette from inside the
    // search box, which is exactly where a user reaches for it.
    QVERIFY(!wouldTypeIntoField(Qt::Key_K, Qt::ControlModifier));
    QVERIFY(!wouldTypeIntoField(Qt::Key_Tab, Qt::ControlModifier | Qt::ShiftModifier));
}

void GamepadDecisionTest::suppressionOnlyAppliesWhileAFieldHasFocus()
{
    // Nothing is suppressed when no field has focus — the pad's letters are how
    // the menu and the search page are opened in the first place.
    QVERIFY(!shouldSuppressKey(Qt::Key_M, Qt::NoModifier, false));
    QVERIFY(shouldSuppressKey(Qt::Key_M, Qt::NoModifier, true));
    QVERIFY(!shouldSuppressKey(Qt::Key_Escape, Qt::NoModifier, true));
    QVERIFY(!shouldSuppressKey(Qt::Key_K, Qt::ControlModifier, true));

    // An editing key is suppressed in a field and is an ordinary key out of one
    // — Backspace is a nav.back binding and must still go back from a grid.
    QVERIFY(shouldSuppressKey(Qt::Key_Backspace, Qt::NoModifier, true));
    QVERIFY(shouldSuppressKey(Qt::Key_Delete, Qt::NoModifier, true));
    QVERIFY(!shouldSuppressKey(Qt::Key_Backspace, Qt::NoModifier, false));
    QVERIFY(!shouldSuppressKey(Qt::Key_Delete, Qt::NoModifier, false));

    // The way out of a field, and the way to submit it, are never suppressed:
    // a pad has no other.
    for (const int key : {Qt::Key_Up, Qt::Key_Down, Qt::Key_Left, Qt::Key_Right, Qt::Key_Return,
                          Qt::Key_Enter, Qt::Key_Tab, Qt::Key_Escape, Qt::Key_PageUp,
                          Qt::Key_PageDown})
        QVERIFY2(!shouldSuppressKey(key, Qt::NoModifier, true), qPrintable(QString::number(key)));
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
