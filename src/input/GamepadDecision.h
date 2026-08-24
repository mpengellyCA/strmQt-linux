#pragma once

#include <algorithm>

#include <QHash>
#include <QList>
#include <QString>

namespace strmqt {

// The two decisions GamepadManager makes about a keystroke it is about to
// synthesize: how fast a held direction may repeat, and whether the key may be
// delivered at all.
//
// Kept pure and free of SDL for the same reason StickDecision.h is: there is no
// gamepad on a build machine, so anything that can only be exercised through a
// device is a rule nobody can check. Both rules below are covered by
// tests/unit/tst_gamepad_decision.cpp.

// ── Auto-repeat ─────────────────────────────────────────────────────────────

struct RepeatTuning
{
    // Long enough that a single flick stays a single step.
    int delayMs = 380;
    // Then acceleration, so crossing a 1300-item grid is a held stick and not a
    // rhythm game.
    int fastMs = 90;
    int fastestMs = 38;
    int stepsBeforeAccel = 6; // steps at fastMs before the rate ramps to fastestMs
    // A held direction in the player is a scrub, not a cursor: the grid ladder's
    // 38 ms floor asks for ~260 s of media per real second and overshoots by
    // minutes before the hand reacts. Floored at 250 ms — four steps a second,
    // ~40 s of media per second, about a disc player's 32× scan: an hour of
    // runtime in 90 s of holding, and still slow enough to stop on a scene.
    //
    // Honest about its own reach: this cannot tell the two player cases apart,
    // because C++ sees the action and the context but not which control has
    // focus. With the OSD scrubber focused, StrmSlider already commits exactly
    // one seek on the final non-repeat release, so the per-step server cost is
    // not there and the floor is purely a feel choice (a 6.5x slower scrub).
    // With the scrubber unfocused, every step really is a 10 s seek and a
    // round-trip. Erring toward the slower, cheaper one.
    int seekFloorMs = 250;
};

// Milliseconds until the next repeat step. `emitted` is the number of steps
// already sent *including* the initial press, so the first call after a hold
// starts sees 2.
inline int repeatIntervalMs(int emitted, bool seeking,
                            const RepeatTuning &tuning = RepeatTuning())
{
    const int interval = emitted > tuning.stepsBeforeAccel + 1 ? tuning.fastestMs : tuning.fastMs;
    return seeking ? std::max(interval, tuning.seekFloorMs) : interval;
}

// Which held directions scrub. Only the horizontal pair, and only in the
// player: Left/Right are what PlayerPage turns into ±10 s, while Up/Down are
// volume and the OSD's own lists still want the fast ladder.
inline bool isSeekRepeat(const QString &context, const QString &actionId)
{
    return context == QLatin1String("player")
        && (actionId == QLatin1String("nav.left") || actionId == QLatin1String("nav.right"));
}

// ── Keys a text field would eat ─────────────────────────────────────────────

// A key a text editor treats as typing rather than as a command. Qt spells
// printable keys as their Latin-1 code point (Key_Space 0x20 … Key_ydiaeresis
// 0xff); every navigation key — arrows, Esc, Return, Tab, Backspace, the
// function keys — lives above 0x01000000, so this is a range check rather than
// a list that has to be maintained.
//
// Ctrl/Alt/Meta make a chord, and a chord is never typing: Ctrl+K must still
// open the command palette with the search box focused. Shift does not count —
// "?" is Shift+/ and is as typable as any other character.
inline bool isTypableKey(int key, int modifiers)
{
    constexpr int kChordModifiers =
        static_cast<int>(Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    if ((modifiers & kChordModifiers) != 0)
        return false;
    return key >= static_cast<int>(Qt::Key_Space) && key <= 0xff;
}

// The pad resolves an action to whatever key it is bound to, and a bound key
// can be an ordinary character (Menu → "M", Y → "/"). Handing that to a focused
// text field is never what the user meant: the field swallows it, the shortcut
// behind it never runs, and on any editor that inserts on key alone it would be
// typed instead. Navigation keys are the exception and must go through — a user
// has to be able to leave a search box with the pad.
inline bool shouldSuppressKey(int key, int modifiers, bool textInputFocused)
{
    return textInputFocused && isTypableKey(key, modifiers);
}

// Tracks which physical devices own a synthesized Qt key. SDL reports removal
// per device, so clearing one controller must not release a key still held by
// another. The bool results say whether a real Qt press/release is required.
class HeldKeyOwnership
{
public:
    bool press(quint32 deviceId, quint64 key)
    {
        ++m_deviceKeys[deviceId][key];
        return ++m_owners[key] == 1;
    }

    bool release(quint32 deviceId, quint64 key)
    {
        auto device = m_deviceKeys.find(deviceId);
        if (device == m_deviceKeys.end())
            return false;
        auto deviceKey = device->find(key);
        if (deviceKey == device->end())
            return false;
        if (--deviceKey.value() == 0)
            device->erase(deviceKey);
        if (device->isEmpty())
            m_deviceKeys.erase(device);
        auto owner = m_owners.find(key);
        if (owner == m_owners.end())
            return false;
        if (--owner.value() > 0)
            return false;
        m_owners.erase(owner);
        return true;
    }

    QList<quint64> releaseDevice(quint32 deviceId)
    {
        const QHash<quint64, int> keys = m_deviceKeys.value(deviceId);
        QList<quint64> finalReleases;
        for (auto it = keys.cbegin(); it != keys.cend(); ++it) {
            for (int count = 0; count < it.value(); ++count) {
                if (release(deviceId, it.key()))
                    finalReleases.append(it.key());
            }
        }
        return finalReleases;
    }

    int owners(quint64 key) const { return m_owners.value(key); }

private:
    QHash<quint32, QHash<quint64, int>> m_deviceKeys;
    QHash<quint64, int> m_owners;
};

} // namespace strmqt
