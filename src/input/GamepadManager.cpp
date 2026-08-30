#include "GamepadManager.h"

#include "core/Log.h"
#include "input/GamepadDecision.h"
#include "input/InputMap.h"
#include "input/StickDecision.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdlib>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QInputMethod>
#include <QKeyEvent>
#include <QTimer>
#include <QWindow>

namespace strmqt {

namespace {

// Stick deadzone, of ±32767. Generous: a worn 360 pad rests well off centre,
// and a false direction that auto-repeats is far worse than one that does not.
constexpr int kStickThreshold = 12'000;
// Release lower than it engages, so a direction held near the edge of the
// deadzone does not stutter on and off.
constexpr int kStickRelease = 8'000;

// The stick is ONE control, not two independent axes. Treating X and Y
// separately means a push toward 2 o'clock crosses both thresholds and fires
// right AND up — which is what made sideways scrolling drift vertically.
//
// Only the dominant axis is allowed to act, and it must dominate by this much.
// Anything inside that wedge is a diagonal, and a diagonal is not a request to
// move in two directions; it is an imprecise request to move in one.
constexpr double kDominanceRatio = 1.5;
// Once an axis owns the stick it keeps it until released, and the other can
// only steal it by dominating this much harder. Without that, a long horizontal
// sweep that wanders slightly up flips to vertical mid-gesture.
constexpr double kTakeoverRatio = 2.2;
// Vertical is the axis that gets triggered by accident while moving sideways
// (a hand rolls up-down far more easily than it rolls left-right), so it has to
// clear a higher bar to start. Horizontal is left at the plain threshold.
constexpr int kVerticalThreshold = 16'000;
// Triggers are analog on this hardware; these two values are deliberately
// different so a trigger resting near the edge cannot chatter.
constexpr int kTriggerPress = 20'000;
constexpr int kTriggerRelease = 12'000;

// Auto-repeat, tuned to feel like a TV UI rather than a text cursor. The ladder
// and the reason the player clamps it live with the pure function in
// GamepadDecision.h, where they can be tested without a device.
constexpr RepeatTuning kRepeat{};

// True while whatever holds focus is a text editor. QGuiApplication::focusObject()
// plus the ImEnabled input-method query is the standard way to ask, and it needs
// no QML dependency — which matters, because this decision cannot be left to
// Main.qml: its single-character shortcuts stand down while a field has focus,
// so a pad button bound to a letter would land in the field instead of firing.
bool textInputHasFocus()
{
    if (!QGuiApplication::focusObject())
        return false;
    return QGuiApplication::inputMethod()->queryFocusObject(Qt::ImEnabled, QVariant()).toBool();
}

// Slots for held directions. The stick and the D-pad get separate slots so
// releasing one does not cancel a direction the other is still holding.
enum DirectionSlot
{
    SlotStickX = 0,
    SlotStickY,
    SlotDpadX,
    SlotDpadY,
    SlotRStickY, // volume in the player; the only right-stick verb
};

} // namespace

GamepadManager::GamepadManager(InputMap *input, QObject *parent) : QObject(parent), m_input(input)
{
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        qCWarning(logApp) << "SDL gamepad init failed:" << SDL_GetError();
        return;
    }
    m_initialized = true;
    qCInfo(logApp) << "SDL3 gamepad support active";

    m_timer = new QTimer(this);
    m_timer->setInterval(gamepadPollIntervalMs(false));
    connect(m_timer, &QTimer::timeout, this, [this] {
        poll();
        pump();
    });
    m_timer->start();
}

GamepadManager::~GamepadManager()
{
    if (!m_initialized)
        return;
    releaseAll();
    // SDL_QuitSubSystem does not close handles this instance opened; a handle
    // that outlives the subsystem is a leak in a process that can re-init.
    const QList<quint32> open = m_pads.keys();
    for (quint32 instanceId : open)
        closePad(instanceId);
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

void GamepadManager::setContext(const QString &context)
{
    if (m_context == context)
        return;
    m_context = context;
    // A direction held across a context switch would keep repeating the *old*
    // context's action; drop everything held rather than translate it. Dropping
    // means releasing: the key is down, and a key left down never commits.
    releaseAll();
}

QString GamepadManager::actionForButton(int sdlButton) const
{
    const bool player = m_context == QLatin1String("player");
    const bool restricted = m_context == QLatin1String("login")
                         || m_context == QLatin1String("overlay");

    switch (sdlButton) {
    // ── Face buttons ───────────────────────────────────────────────────────
    // A while browsing is handled by handleSelectButton() instead, which needs
    // to tell a tap from a hold; this is the answer for the contexts that have
    // no hold gesture.
    case SDL_GAMEPAD_BUTTON_SOUTH: // A
        return player ? QStringLiteral("player.togglePause") : QStringLiteral("nav.select");
    case SDL_GAMEPAD_BUTTON_EAST: // B
        return QStringLiteral("nav.back");
    case SDL_GAMEPAD_BUTTON_WEST: // X
        if (restricted)
            return {};
        return player ? QStringLiteral("player.cycleAudio")
                      : QStringLiteral("app.commandPalette");
    case SDL_GAMEPAD_BUTTON_NORTH: // Y
        if (restricted)
            return {};
        return player ? QStringLiteral("player.cycleSubtitle")
                      : QStringLiteral("library.search");

    // ── Shoulders: the pair that changes what you are looking at ───────────
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        if (restricted)
            return {};
        return player ? QStringLiteral("player.seekBackwardLong")
                      : QStringLiteral("nav.previousTab");
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        if (restricted)
            return {};
        return player ? QStringLiteral("player.seekForwardLong")
                      : QStringLiteral("nav.nextTab");

    // ── Centre cluster ─────────────────────────────────────────────────────
    // Start is the Menu button on a 360/One pad and is what a TV UI opens its
    // menu with; in the player there is no rail to open, so it reveals the OSD.
    case SDL_GAMEPAD_BUTTON_START:
        if (restricted)
            return {};
        return player ? QStringLiteral("player.toggleOsd") : QStringLiteral("app.toggleMenu");
    // Back is "View" on an Xbox One pad.
    case SDL_GAMEPAD_BUTTON_BACK:
        return player ? QStringLiteral("player.stop") : QStringLiteral("nav.back");
    case SDL_GAMEPAD_BUTTON_GUIDE:
        if (restricted)
            return {};
        return player ? QString() : QStringLiteral("app.shortcuts");
    // L3: the short way back to the film. Leaving the player keeps it
    // playing on the docked bar, and without this getting back to full screen
    // from a pad is walking the whole navigation stack to the bar and
    // pressing A on it.
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
        if (restricted)
            return {};
        return QStringLiteral("player.toggleView");
    // The docked bar is unreachable from a pad otherwise: it takes focus by a
    // click or by Tab, and a pad has neither. Browse only — in the player there
    // is no bar, and the right stick's vertical axis is volume there.
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
        if (restricted)
            return {};
        return player ? QString() : QStringLiteral("player.focusBar");
    default:
        return QString();
    }
}

void GamepadManager::poll()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_GAMEPAD_ADDED: {
            const quint32 instanceId = event.gdevice.which;
            // SDL re-announces devices it already told us about (a re-plug that
            // reuses an id, a subsystem re-init), and opening twice would leak
            // the first handle.
            releaseDevice(instanceId);
            closePad(instanceId);
            SDL_Gamepad *pad = SDL_OpenGamepad(instanceId);
            qCInfo(logApp) << "gamepad connected:" << (pad ? SDL_GetGamepadName(pad) : "unknown");
            if (pad)
                m_pads.insert(instanceId, pad);
            break;
        }
        case SDL_EVENT_GAMEPAD_REMOVED:
            qCInfo(logApp) << "gamepad disconnected";
            // The handle SDL gave us on connect goes back on disconnect; a
            // removal for a device we never opened closes nothing.
            closePad(event.gdevice.which);
            // Another controller may still own the same held Qt key.
            releaseDevice(event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            handleButton(event.gbutton.which, event.gbutton.button, true);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            handleButton(event.gbutton.which, event.gbutton.button, false);
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            handleAxis(event.gaxis.which, event.gaxis.axis, event.gaxis.value);
            break;
        default:
            break;
        }
    }

    const int interval = gamepadPollIntervalMs(!m_pads.isEmpty());
    if (m_timer->interval() != interval)
        m_timer->setInterval(interval);
}

void GamepadManager::handleButton(quint32 deviceId, int sdlButton, bool pressed)
{
    // D-pad directions repeat; everything else is a discrete press so a held
    // A does not fire the same item over and over.
    switch (sdlButton) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        setDirection(deviceId, SlotDpadY, QStringLiteral("nav.up"), pressed);
        return;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        setDirection(deviceId, SlotDpadY, QStringLiteral("nav.down"), pressed);
        return;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        setDirection(deviceId, SlotDpadX, QStringLiteral("nav.left"), pressed);
        return;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        setDirection(deviceId, SlotDpadX, QStringLiteral("nav.right"), pressed);
        return;
    default:
        break;
    }

    if (sdlButton == SDL_GAMEPAD_BUTTON_SOUTH && handleSelectButton(deviceId, pressed))
        return;

    if (!pressed)
        return;
    const QString actionId = actionForButton(sdlButton);
    if (!actionId.isEmpty())
        tap(actionId);
}

bool GamepadManager::handleSelectButton(quint32 deviceId, bool pressed)
{
    // Only where there is something to open a menu ON. The player has no items,
    // and login and the overlays are one-decision surfaces where a delayed
    // select would be a regression bought for nothing.
    if (m_context != QLatin1String("browse") && m_context != QLatin1String("music"))
        return false;

    DeviceState &state = m_deviceStates[deviceId];
    if (pressed) {
        state.selectHeldFor.start();
        state.selectHold.press();
        return true;
    }
    if (state.selectHold.release() == SelectGesture::Select)
        tap(QStringLiteral("nav.select"));
    return true;
}

void GamepadManager::handleAxis(quint32 deviceId, int axis, int value)
{
    DeviceState &state = m_deviceStates[deviceId];
    // Triggers are analog but used as buttons, with separate press/release
    // thresholds so a trigger held near the edge cannot chatter.
    if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
        const bool wasDown = state.axisState.value(axis, 0) != 0;
        const bool isDown = wasDown ? value > kTriggerRelease : value > kTriggerPress;
        if (isDown == wasDown)
            return;
        state.axisState[axis] = isDown ? 1 : 0;
        if (!isDown)
            return;
        if (m_context == QLatin1String("login") || m_context == QLatin1String("overlay"))
            return;

        const bool player = m_context == QLatin1String("player");
        const bool left = axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
        if (player)
            tap(left ? QStringLiteral("player.seekBackward")
                     : QStringLiteral("player.seekForward"));
        else
            // Not pageUp/pageDown any more: the shell resolves a letter step to
            // the alphabet strip where the page has one and to a page of the
            // list where it does not (Main.qml jumpLetter), so one pair of
            // triggers covers both and the useful one wins on a long library.
            tap(left ? QStringLiteral("nav.previousLetter")
                     : QStringLiteral("nav.nextLetter"));
        return;
    }

    // The right stick's vertical axis is the pad's volume control — the one
    // player verb with no button left over for it, and a slider a pad cannot
    // focus. Player context only: everywhere else the stick keeps having no
    // verb, and the "+"/"-" keys it would send have no handler either. A held
    // direction auto-repeats through the same machinery as the D-pad.
    if (axis == SDL_GAMEPAD_AXIS_RIGHTY) {
        const int held = state.axisState.value(axis, 0); // 0, or ±1 for up/down
        int dir = 0;
        if (m_context == QLatin1String("player")) {
            if (value < -kStickThreshold)
                dir = -1;
            else if (value > kStickThreshold)
                dir = 1;
            else if (std::abs(value) > kStickRelease)
                dir = held; // hysteresis: an engaged direction holds near the edge
        }
        if (dir == held)
            return;
        state.axisState[axis] = dir;
        setDirection(deviceId, SlotRStickY,
                     dir < 0 ? QStringLiteral("player.volumeUp")
                             : QStringLiteral("player.volumeDown"),
                     dir != 0);
        return;
    }

    if (axis != SDL_GAMEPAD_AXIS_LEFTX && axis != SDL_GAMEPAD_AXIS_LEFTY)
        return; // the right stick's horizontal axis has no verb behind it

    // Store, then decide from BOTH axes: a single axis value cannot tell a
    // deliberate push from the incidental half of a diagonal.
    if (axis == SDL_GAMEPAD_AXIS_LEFTX)
        state.stickX = value;
    else
        state.stickY = value;
    evaluateStick(deviceId);
}

void GamepadManager::evaluateStick(quint32 deviceId)
{
    DeviceState &state = m_deviceStates[deviceId];
    const StickTuning tuning{kStickThreshold, kStickRelease, kVerticalThreshold,
                             kDominanceRatio, kTakeoverRatio};
    const StickAxis owned = state.stickAxis == SlotStickX  ? StickAxis::Horizontal
                          : state.stickAxis == SlotStickY  ? StickAxis::Vertical
                                                       : StickAxis::None;
    const StickAxis chosen = decideStickAxis(state.stickX, state.stickY, owned, tuning);

    if (chosen == StickAxis::None) {
        setDirection(deviceId, SlotStickX, QString(), false);
        setDirection(deviceId, SlotStickY, QString(), false);
        state.stickAxis = -1;
        return;
    }

    const bool horizontal = chosen == StickAxis::Horizontal;
    const int slot = horizontal ? SlotStickX : SlotStickY;
    const QString actionId =
        horizontal ? (state.stickX < 0 ? QStringLiteral("nav.left") : QStringLiteral("nav.right"))
                   : (state.stickY < 0 ? QStringLiteral("nav.up") : QStringLiteral("nav.down"));

    // Handing the stick to the other axis must release the first, or a direction
    // it was holding would keep repeating.
    setDirection(deviceId, horizontal ? SlotStickY : SlotStickX, QString(), false);
    state.stickAxis = slot;
    setDirection(deviceId, slot, actionId, true);
}

void GamepadManager::setDirection(quint32 deviceId, int slot, const QString &actionId, bool active)
{
    if (!active || actionId.isEmpty()) {
        releaseDirection(deviceId, slot);
        return;
    }
    DeviceState &state = m_deviceStates[deviceId];
    const auto it = state.held.constFind(slot);
    if (it != state.held.constEnd() && it->actionId == actionId)
        return; // already holding this direction
    // Turning around on one slot ends the old direction properly rather than
    // dropping its key on the floor still pressed.
    releaseDirection(deviceId, slot);

    Repeat repeat;
    repeat.actionId = actionId;
    repeat.heldFor.start();
    repeat.emitted = 1;
    repeat.nextAtMs = kRepeat.delayMs;
    repeat.seeking = isSeekRepeat(m_context, actionId);
    if (resolveKey(actionId, &repeat.key, &repeat.modifiers)) {
        m_input->noteInput(QStringLiteral("gamepad"));
        // The key goes DOWN and stays down for the whole hold, exactly as it
        // would on a keyboard: the first step is immediate, only the repeat
        // waits, and the release that ends the gesture comes in
        // releaseDirection(). That release is not an auto-repeat, and it is the
        // event a control commits on.
        pressHeldKey(deviceId, repeat.key, repeat.modifiers);
    }
    state.held.insert(slot, repeat);
}

void GamepadManager::releaseDirection(quint32 deviceId, int slot)
{
    auto stateIt = m_deviceStates.find(deviceId);
    if (stateIt == m_deviceStates.end())
        return;
    DeviceState &state = stateIt.value();
    const auto it = state.held.constFind(slot);
    if (it == state.held.constEnd())
        return;
    const Repeat repeat = *it;
    state.held.erase(it);
    if (repeat.key != 0)
        releaseHeldKey(deviceId, repeat.key, repeat.modifiers);
}

void GamepadManager::releaseDevice(quint32 deviceId)
{
    auto it = m_deviceStates.find(deviceId);
    if (it == m_deviceStates.end())
        return;
    const QList<int> heldSlots = it->held.keys();
    for (int slot : heldSlots)
        releaseDirection(deviceId, slot);
    m_deviceStates.erase(it);
}

void GamepadManager::releaseAll()
{
    const QList<quint32> devices = m_deviceStates.keys();
    for (quint32 deviceId : devices)
        releaseDevice(deviceId);
}

void GamepadManager::pump()
{
    for (DeviceState &state : m_deviceStates) {
        // The hold matures here rather than on the release, so the menu opens
        // under the thumb while the button is still down — which is what tells
        // the user the gesture is a hold at all. The release that follows is
        // then a no-op (SelectHold::release).
        if (state.selectHold.tick(state.selectHeldFor.elapsed()) == SelectGesture::ContextMenu)
            tap(QStringLiteral("nav.contextMenu"));
        for (auto it = state.held.begin(); it != state.held.end(); ++it) {
            Repeat &repeat = it.value();
            const qint64 heldMs = repeat.heldFor.elapsed();
            if (heldMs < repeat.nextAtMs)
                continue;
            if (repeat.key != 0) {
                m_input->noteInput(QStringLiteral("gamepad"));
                // A repeat step is a release/press pair *flagged as auto-repeat*,
                // which is the shape a keyboard delivers one in. The flag is the
                // whole point: controls already read it — StrmSlider nudges on every
                // press and commits only on a release that is not a repeat — so an
                // unflagged step committed a seek, and a server round-trip, per
                // step of the hold.
                sendKey(repeat.key, repeat.modifiers, false, true);
                sendKey(repeat.key, repeat.modifiers, true, true);
            }
            ++repeat.emitted;
            const int interval = repeatIntervalMs(repeat.emitted, repeat.seeking, kRepeat);
            // Advance from the scheduled time, not from now, so a late frame does
            // not slow the whole repeat down.
            repeat.nextAtMs += interval;
            if (repeat.nextAtMs < heldMs)
                repeat.nextAtMs = heldMs + interval;
        }
    }
}

void GamepadManager::tap(const QString &actionId)
{
    int key = 0;
    int modifiers = 0;
    if (!resolveKey(actionId, &key, &modifiers))
        return;
    m_input->noteInput(QStringLiteral("gamepad"));
    // Discrete: neither half is an auto-repeat, so a control that commits on a
    // plain release commits exactly once.
    sendKey(key, modifiers, true, false);
    sendKey(key, modifiers, false, false);
}

bool GamepadManager::resolveKey(const QString &actionId, int *key, int *modifiers) const
{
    *key = 0;
    *modifiers = 0;
    if (!m_input)
        return false;
    const int resolved = m_input->keyFor(actionId);
    if (resolved == 0) {
        // A binding that resolves to no single key cannot be synthesized. Say
        // so rather than silently doing nothing, which is how the old LT/RT and
        // right-stick hints survived while being wired to nothing at all.
        qCDebug(logApp) << "gamepad: no single-key binding for" << actionId;
        return false;
    }
    const int resolvedModifiers = m_input->modifiersFor(actionId);
    if (shouldSuppressKey(resolved, resolvedModifiers, textInputHasFocus())) {
        // Menu resolves to "M" and Y to "/", and a text field takes both as
        // typing: it eats the key, the shortcut behind it never runs, and an
        // editor that inserts on the key alone would print it. The button goes
        // dead while a field has focus instead — B/Esc, the arrows and Return
        // are not typable and still get through, so the field can be left.
        qCDebug(logApp) << "gamepad: not delivering typable key for" << actionId
                        << "— a text field has focus";
        return false;
    }
    *key = resolved;
    *modifiers = resolvedModifiers;
    return true;
}

void GamepadManager::sendKey(int qtKey, int modifiers, bool pressed, bool autoRepeat)
{
    QWindow *window = QGuiApplication::focusWindow();
    if (!window)
        return;
    // The text is deliberately empty: this is a command, not typing.
    auto *event = new QKeyEvent(pressed ? QEvent::KeyPress : QEvent::KeyRelease, qtKey,
                                static_cast<Qt::KeyboardModifiers>(modifiers), QString(),
                                autoRepeat);
    QCoreApplication::postEvent(window, event);
}

void GamepadManager::pressHeldKey(quint32 deviceId, int qtKey, int modifiers)
{
    const quint64 keyId = (quint64(quint32(modifiers)) << 32) | quint32(qtKey);
    if (m_keyOwners.press(deviceId, keyId))
        sendKey(qtKey, modifiers, true, false);
}

void GamepadManager::releaseHeldKey(quint32 deviceId, int qtKey, int modifiers)
{
    const quint64 keyId = (quint64(quint32(modifiers)) << 32) | quint32(qtKey);
    if (m_keyOwners.release(deviceId, keyId))
        sendKey(qtKey, modifiers, false, false);
}

void GamepadManager::closePad(quint32 instanceId)
{
    SDL_Gamepad *pad = m_pads.take(instanceId);
    if (!pad)
        return;
    // take() first: closing is the only place the handle is released, so it can
    // never be closed twice even if SDL repeats a removal.
    SDL_CloseGamepad(pad);
}

} // namespace strmqt
