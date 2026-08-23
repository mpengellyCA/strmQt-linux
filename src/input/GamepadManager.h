#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>

class QTimer;

// Opaque SDL handle, forward-declared so this header stays SDL-free: it is
// included by Application.cpp, which knows nothing about SDL.
struct SDL_Gamepad;

namespace strmqt {

class InputMap;

// SDL3 gamepad → synthesized key events, resolved through InputMap so the
// gamepad and the keyboard cannot drift apart (PLAN §3.7, ARCHITECTURE.md, F6).
//
// A button does not carry a hardcoded Qt key. It resolves to an *action id*,
// which InputMap turns into whatever sequence that action is currently bound
// to — so rebinding a shortcut rebinds the pad with it, and a gamepad hint
// shown in the shortcut sheet is the binding that actually fires.
//
// Layout targets the Xbox 360 / Xbox One pad, which is the PC standard and the
// layout SDL's own "gamepad" abstraction is modelled on; anything SDL maps to
// that abstraction (DualSense, Switch Pro, 8BitDo) lands in the right place.
//
// Two things the previous version did not do, and their absence was the whole
// of the "gamepad works but is limited" report:
//   · a held stick or D-pad emitted ONE key press, so navigating a 1300-item
//     grid meant 1300 discrete flicks. Directions now auto-repeat, and the
//     repeat accelerates the longer it is held.
//   · the triggers and the shoulder buttons were either unread or fixed to
//     seeking, so there was no way to move between libraries at all.
//
// A synthesized event says what it is. A held direction presses its key and
// leaves it down, repeats are flagged as auto-repeat, and the release at the
// end is the only plain one — the same shape a keyboard produces. Controls
// already read that flag (StrmSlider nudges per step and commits one seek on a
// non-repeat release), so a hold that lied about being a repeat committed a
// seek per step: up to 26 server round-trips a second.
class GamepadManager : public QObject
{
    Q_OBJECT

public:
    explicit GamepadManager(InputMap *input, QObject *parent = nullptr);
    ~GamepadManager() override;

    bool available() const { return m_initialized; }

    // "browse" or "player" — the same button means different things in each,
    // which is what lets LB/RB change library while browsing and jump 60 s
    // during playback. Set by Application from the player's active state.
    void setContext(const QString &context);
    QString context() const { return m_context; }

private:
    // A direction being held, and how long it has been held for.
    struct Repeat
    {
        QString actionId;
        QElapsedTimer heldFor;
        int emitted = 0;
        qint64 nextAtMs = 0;
        // Resolved once, when the hold starts. A rebind mid-hold would
        // otherwise release a different key than the one pressed, leaving the
        // first one down for the rest of the session. Zero means the hold sends
        // nothing (no single-key binding, or suppressed by a focused field).
        int key = 0;
        int modifiers = 0;
        bool seeking = false; // clamps the repeat rate: a scrub is not a cursor
    };

    void poll();
    void pump();
    void handleButton(int sdlButton, bool pressed);
    void handleAxis(int axis, int value);
    // Decides a single direction from BOTH stick axes. The stick is one
    // control: a diagonal is an imprecise request to move one way, not a
    // request to move two ways at once.
    void evaluateStick();
    // Digital view of a direction: starts a hold, or ends one.
    void setDirection(int slot, const QString &actionId, bool active);
    // Ends a hold, sending the plain release that tells a control the gesture
    // is over. Every path that forgets a hold goes through here.
    void releaseDirection(int slot);
    void releaseAll();
    // A discrete press: press and release back to back, neither a repeat.
    void tap(const QString &actionId);
    // Current binding for an action, and whether it may be delivered at all.
    // False leaves *key and *modifiers zeroed.
    bool resolveKey(const QString &actionId, int *key, int *modifiers) const;
    void sendKey(int qtKey, int modifiers, bool pressed, bool autoRepeat);
    // Closes an open SDL handle, if this instance id has one.
    void closePad(quint32 instanceId);
    // Action for a button in the current context, or an empty string.
    QString actionForButton(int sdlButton) const;

    InputMap *m_input = nullptr;
    bool m_initialized = false;
    QString m_context = QStringLiteral("browse");
    QTimer *m_timer = nullptr;

    // Slot → held direction. Slots are stable ids (stick X, stick Y, D-pad X,
    // D-pad Y) so the stick and the D-pad cannot fight over one another's state.
    QHash<int, Repeat> m_held;
    // Latched digital state per analog axis, so a trigger reports one press.
    QHash<int, int> m_axisState;
    // SDL instance id → open handle. SDL hands out a handle on connect and
    // expects it back on disconnect; keeping the map is also what makes a
    // removal event for a device we never opened a no-op rather than a guess.
    QHash<quint32, SDL_Gamepad *> m_pads;

    // Raw left-stick position, and which axis currently owns it (-1 = neither).
    // Ownership is what stops a long horizontal sweep that wanders slightly up
    // from flipping to vertical halfway through.
    int m_stickX = 0;
    int m_stickY = 0;
    int m_stickAxis = -1;
};

} // namespace strmqt
