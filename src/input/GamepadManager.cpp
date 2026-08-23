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

constexpr int kPollIntervalMs = 16;

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
    m_timer->setInterval(kPollIntervalMs);
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
    m_stickAxis = -1;
}

QString GamepadManager::actionForButton(int sdlButton) const
{
    const bool player = m_context == QLatin1String("player");

    switch (sdlButton) {
    // ── Face buttons ───────────────────────────────────────────────────────
    case SDL_GAMEPAD_BUTTON_SOUTH: // A
        return player ? QStringLiteral("player.togglePause") : QStringLiteral("nav.select");
    case SDL_GAMEPAD_BUTTON_EAST: // B
        return QStringLiteral("nav.back");
    case SDL_GAMEPAD_BUTTON_WEST: // X
        return player ? QStringLiteral("player.cycleAudio")
                      : QStringLiteral("app.commandPalette");
    case SDL_GAMEPAD_BUTTON_NORTH: // Y
        return player ? QStringLiteral("player.cycleSubtitle")
                      : QStringLiteral("library.search");

    // ── Shoulders: the pair that changes what you are looking at ───────────
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        return player ? QStringLiteral("player.seekBackwardLong")
                      : QStringLiteral("nav.previousTab");
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        return player ? QStringLiteral("player.seekForwardLong")
                      : QStringLiteral("nav.nextTab");

    // ── Centre cluster ─────────────────────────────────────────────────────
    // Start is the Menu button on a 360/One pad and is what a TV UI opens its
    // menu with; in the player there is no rail to open, so it reveals the OSD.
    case SDL_GAMEPAD_BUTTON_START:
        return player ? QStringLiteral("player.toggleOsd") : QStringLiteral("app.toggleMenu");
    // Back is "View" on an Xbox One pad.
    case SDL_GAMEPAD_BUTTON_BACK:
        return player ? QStringLiteral("player.stop") : QStringLiteral("nav.back");
    case SDL_GAMEPAD_BUTTON_GUIDE:
        return player ? QString() : QStringLiteral("app.shortcuts");
    // The docked bar is unreachable from a pad otherwise: it takes focus by a
    // click or by Tab, and a pad has neither. Browse only — in the player there
    // is no bar, and the right stick is where a volume verb would land if
    // PlayerController ever grows one.
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
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
            // Whatever was held is no longer held by anything.
            releaseAll();
            m_axisState.clear();
            m_stickX = 0;
            m_stickY = 0;
            m_stickAxis = -1;
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            handleButton(event.gbutton.button, true);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            handleButton(event.gbutton.button, false);
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            handleAxis(event.gaxis.axis, event.gaxis.value);
            break;
        default:
            break;
        }
    }
}

void GamepadManager::handleButton(int sdlButton, bool pressed)
{
    // D-pad directions repeat; everything else is a discrete press so a held
    // A does not fire the same item over and over.
    switch (sdlButton) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        setDirection(SlotDpadY, QStringLiteral("nav.up"), pressed);
        return;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        setDirection(SlotDpadY, QStringLiteral("nav.down"), pressed);
        return;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        setDirection(SlotDpadX, QStringLiteral("nav.left"), pressed);
        return;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        setDirection(SlotDpadX, QStringLiteral("nav.right"), pressed);
        return;
    default:
        break;
    }

    if (!pressed)
        return;
    const QString actionId = actionForButton(sdlButton);
    if (!actionId.isEmpty())
        tap(actionId);
}

void GamepadManager::handleAxis(int axis, int value)
{
    // Triggers are analog but used as buttons, with separate press/release
    // thresholds so a trigger held near the edge cannot chatter.
    if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
        const bool wasDown = m_axisState.value(axis, 0) != 0;
        const bool isDown = wasDown ? value > kTriggerRelease : value > kTriggerPress;
        if (isDown == wasDown)
            return;
        m_axisState[axis] = isDown ? 1 : 0;
        if (!isDown)
            return;

        const bool player = m_context == QLatin1String("player");
        const bool left = axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
        if (player)
            tap(left ? QStringLiteral("player.seekBackward")
                     : QStringLiteral("player.seekForward"));
        else
            tap(left ? QStringLiteral("nav.pageUp") : QStringLiteral("nav.pageDown"));
        return;
    }

    if (axis != SDL_GAMEPAD_AXIS_LEFTX && axis != SDL_GAMEPAD_AXIS_LEFTY)
        return; // the right stick has no verb behind it; do not pretend it does

    // Store, then decide from BOTH axes: a single axis value cannot tell a
    // deliberate push from the incidental half of a diagonal.
    if (axis == SDL_GAMEPAD_AXIS_LEFTX)
        m_stickX = value;
    else
        m_stickY = value;
    evaluateStick();
}

void GamepadManager::evaluateStick()
{
    const StickTuning tuning{kStickThreshold, kStickRelease, kVerticalThreshold,
                             kDominanceRatio, kTakeoverRatio};
    const StickAxis owned = m_stickAxis == SlotStickX  ? StickAxis::Horizontal
                          : m_stickAxis == SlotStickY  ? StickAxis::Vertical
                                                       : StickAxis::None;
    const StickAxis chosen = decideStickAxis(m_stickX, m_stickY, owned, tuning);

    if (chosen == StickAxis::None) {
        setDirection(SlotStickX, QString(), false);
        setDirection(SlotStickY, QString(), false);
        m_stickAxis = -1;
        return;
    }

    const bool horizontal = chosen == StickAxis::Horizontal;
    const int slot = horizontal ? SlotStickX : SlotStickY;
    const QString actionId =
        horizontal ? (m_stickX < 0 ? QStringLiteral("nav.left") : QStringLiteral("nav.right"))
                   : (m_stickY < 0 ? QStringLiteral("nav.up") : QStringLiteral("nav.down"));

    // Handing the stick to the other axis must release the first, or a direction
    // it was holding would keep repeating.
    setDirection(horizontal ? SlotStickY : SlotStickX, QString(), false);
    m_stickAxis = slot;
    setDirection(slot, actionId, true);
}

void GamepadManager::setDirection(int slot, const QString &actionId, bool active)
{
    if (!active || actionId.isEmpty()) {
        releaseDirection(slot);
        return;
    }
    const auto it = m_held.constFind(slot);
    if (it != m_held.constEnd() && it->actionId == actionId)
        return; // already holding this direction
    // Turning around on one slot ends the old direction properly rather than
    // dropping its key on the floor still pressed.
    releaseDirection(slot);

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
        sendKey(repeat.key, repeat.modifiers, true, false);
    }
    m_held.insert(slot, repeat);
}

void GamepadManager::releaseDirection(int slot)
{
    const auto it = m_held.constFind(slot);
    if (it == m_held.constEnd())
        return;
    const Repeat repeat = *it;
    m_held.erase(it);
    if (repeat.key != 0)
        sendKey(repeat.key, repeat.modifiers, false, false);
}

void GamepadManager::releaseAll()
{
    // `slots` is a Qt keyword, hence the name.
    const QList<int> heldSlots = m_held.keys();
    for (int slot : heldSlots)
        releaseDirection(slot);
}

void GamepadManager::pump()
{
    if (m_held.isEmpty())
        return;
    for (auto it = m_held.begin(); it != m_held.end(); ++it) {
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
