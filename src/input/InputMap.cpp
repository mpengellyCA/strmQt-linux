#include "InputMap.h"

#include "core/Log.h"

#include <QVariantMap>

#include <algorithm>
#include <utility>

namespace strmqt {

namespace {

const auto kOverrideGroup = QStringLiteral("input/binding/");
const auto kLastDeviceKey = QStringLiteral("input/lastDevice");

// ── Key-name table ────────────────────────────────────────────────────────────
// QtGui is out of bounds in this layer (AGENTS.md), so QKeySequence cannot do
// the parsing. The spellings below are the ones QKeySequence::fromString()
// accepts, which is what QML `Shortcut.sequence` runs through — so every name
// here round-trips into a working Shortcut.
struct KeyName
{
    const char *name;
    int key; // Qt::Key
};

const KeyName kNamedKeys[] = {
    {"Space", Qt::Key_Space},
    {"Esc", Qt::Key_Escape},
    {"Tab", Qt::Key_Tab},
    {"Backtab", Qt::Key_Backtab},
    {"Backspace", Qt::Key_Backspace},
    {"Return", Qt::Key_Return},
    {"Enter", Qt::Key_Enter},
    {"Ins", Qt::Key_Insert},
    {"Del", Qt::Key_Delete},
    {"Home", Qt::Key_Home},
    {"End", Qt::Key_End},
    {"PgUp", Qt::Key_PageUp},
    {"PgDown", Qt::Key_PageDown},
    {"Left", Qt::Key_Left},
    {"Right", Qt::Key_Right},
    {"Up", Qt::Key_Up},
    {"Down", Qt::Key_Down},
    {"Back", Qt::Key_Back},
    {"Forward", Qt::Key_Forward},
    {"+", Qt::Key_Plus},
    {"-", Qt::Key_Minus},
    {"=", Qt::Key_Equal},
    {"/", Qt::Key_Slash},
    {"\\", Qt::Key_Backslash},
    {",", Qt::Key_Comma},
    {".", Qt::Key_Period},
    {";", Qt::Key_Semicolon},
    {"'", Qt::Key_Apostrophe},
    {"[", Qt::Key_BracketLeft},
    {"]", Qt::Key_BracketRight},
    {"?", Qt::Key_Question},
    {"*", Qt::Key_Asterisk},
};

// Aliases accepted on input but never emitted (canonical spelling wins).
const KeyName kKeyAliases[] = {
    {"Escape", Qt::Key_Escape},  {"Insert", Qt::Key_Insert},     {"Delete", Qt::Key_Delete},
    {"PageUp", Qt::Key_PageUp},  {"PageDown", Qt::Key_PageDown}, {"PgDn", Qt::Key_PageDown},
    {"Plus", Qt::Key_Plus},      {"Minus", Qt::Key_Minus},       {"Slash", Qt::Key_Slash},
    {"Spacebar", Qt::Key_Space},
};

struct ParsedSequence
{
    int key = 0;
    int modifiers = 0;
    bool valid = false;
};

QString canonicalKeyName(int key)
{
    for (const KeyName &entry : kNamedKeys) {
        if (entry.key == key)
            return QString::fromLatin1(entry.name);
    }
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return QChar(QLatin1Char(static_cast<char>('A' + (key - Qt::Key_A))));
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return QChar(QLatin1Char(static_cast<char>('0' + (key - Qt::Key_0))));
    if (key >= Qt::Key_F1 && key <= Qt::Key_F35)
        return QStringLiteral("F%1").arg(key - Qt::Key_F1 + 1);
    return {};
}

int keyFromName(const QString &raw)
{
    const QString name = raw.trimmed();
    if (name.isEmpty())
        return 0;
    for (const KeyName &entry : kNamedKeys) {
        if (name.compare(QLatin1String(entry.name), Qt::CaseInsensitive) == 0)
            return entry.key;
    }
    for (const KeyName &entry : kKeyAliases) {
        if (name.compare(QLatin1String(entry.name), Qt::CaseInsensitive) == 0)
            return entry.key;
    }
    if (name.size() == 1) {
        const QChar c = name.at(0).toUpper();
        if (c >= QLatin1Char('A') && c <= QLatin1Char('Z'))
            return Qt::Key_A + (c.unicode() - u'A');
        if (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
            return Qt::Key_0 + (c.unicode() - u'0');
        return 0;
    }
    if (name.size() >= 2 && (name.at(0) == QLatin1Char('F') || name.at(0) == QLatin1Char('f'))) {
        bool ok = false;
        const int n = QStringView(name).mid(1).toInt(&ok);
        if (ok && n >= 1 && n <= 35)
            return Qt::Key_F1 + (n - 1);
    }
    return 0;
}

ParsedSequence parseSequence(const QString &raw)
{
    ParsedSequence out;
    const QString text = raw.trimmed();
    if (text.isEmpty())
        return out;

    // A bare "+" (volume up) is a key, not a separator.
    if (text == QLatin1String("+")) {
        out.key = Qt::Key_Plus;
        out.valid = true;
        return out;
    }

    const QStringList tokens = text.split(QLatin1Char('+'), Qt::KeepEmptyParts);
    for (qsizetype i = 0; i < tokens.size(); ++i) {
        const QString token = tokens.at(i).trimmed();
        const bool last = (i == tokens.size() - 1);
        // "Ctrl++" splits to {"Ctrl", "", ""}: an empty trailing token means '+'.
        if (token.isEmpty()) {
            if (i == tokens.size() - 2 && tokens.last().trimmed().isEmpty()) {
                out.key = Qt::Key_Plus;
                out.valid = true;
                return out;
            }
            if (!last)
                return {};
            continue;
        }
        if (!last) {
            if (token.compare(QLatin1String("Ctrl"), Qt::CaseInsensitive) == 0 ||
                token.compare(QLatin1String("Control"), Qt::CaseInsensitive) == 0)
                out.modifiers |= Qt::ControlModifier;
            else if (token.compare(QLatin1String("Alt"), Qt::CaseInsensitive) == 0)
                out.modifiers |= Qt::AltModifier;
            else if (token.compare(QLatin1String("Shift"), Qt::CaseInsensitive) == 0)
                out.modifiers |= Qt::ShiftModifier;
            else if (token.compare(QLatin1String("Meta"), Qt::CaseInsensitive) == 0 ||
                     token.compare(QLatin1String("Super"), Qt::CaseInsensitive) == 0)
                out.modifiers |= Qt::MetaModifier;
            else
                return {}; // unknown modifier
            continue;
        }
        out.key = keyFromName(token);
        if (out.key == 0)
            return {};
        out.valid = true;
    }
    return out;
}

// Is this a key a focused text-editing item would use itself? Two groups, and
// both are "typing" from the field's point of view:
//
//  · keys that put a character in — every letter, digit, the punctuation the
//    table above can spell, and Space, which is the one the string-length rule
//    this replaced got wrong;
//  · keys that edit or move the caret — a Delete bound to a shortcut is no
//    better than a letter if it fires while the user is fixing a typo.
//
// Escape is deliberately absent: QQuickTextInput does not consume it, and it is
// how every overlay in this app is dismissed from inside a field.
bool isTypableKey(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return true;
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return true;
    switch (key) {
    case Qt::Key_Space:
    case Qt::Key_Plus:
    case Qt::Key_Minus:
    case Qt::Key_Equal:
    case Qt::Key_Slash:
    case Qt::Key_Backslash:
    case Qt::Key_Comma:
    case Qt::Key_Period:
    case Qt::Key_Semicolon:
    case Qt::Key_Apostrophe:
    case Qt::Key_BracketLeft:
    case Qt::Key_BracketRight:
    case Qt::Key_Question:
    case Qt::Key_Asterisk:
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
    case Qt::Key_Insert:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        return true;
    default:
        return false;
    }
}

QString formatSequence(int key, int modifiers)
{
    const QString name = canonicalKeyName(key);
    if (name.isEmpty())
        return {};
    QString out;
    if (modifiers & Qt::ControlModifier)
        out += QStringLiteral("Ctrl+");
    if (modifiers & Qt::AltModifier)
        out += QStringLiteral("Alt+");
    if (modifiers & Qt::ShiftModifier)
        out += QStringLiteral("Shift+");
    if (modifiers & Qt::MetaModifier)
        out += QStringLiteral("Meta+");
    return out + name;
}

// ── Default catalogue ─────────────────────────────────────────────────────────
// Every entry reproduces a binding that exists in the tree today (Main.qml,
// PlayerPage.qml, GamepadManager.cpp) or is required by ARCHITECTURE.md Nothing
// here invents a keyboard shortcut the app does not already honour, with the
// single documented exception of the volume actions, which PLAN §3.7 mandates
// and no page handles yet.
const QList<InputMap::ActionDef> &catalogue()
{
    static const QList<InputMap::ActionDef> defs = {
        // Navigation — arrows/Return/Esc are handled by Qt focus handling and
        // Main.qml's StackView Keys handlers; listed so the shortcut sheet and
        // the remap UI can show and rebind them.
        {QStringLiteral("nav.up"),
         QObject::tr("Move up"),
         QStringLiteral("Navigation"),
         QString::fromLatin1(InputMap::kContextBrowse),
         {QStringLiteral("Up")},
         QObject::tr("D-Pad Up / Left Stick Up")},
        {QStringLiteral("nav.down"),
         QObject::tr("Move down"),
         QStringLiteral("Navigation"),
         QString::fromLatin1(InputMap::kContextBrowse),
         {QStringLiteral("Down")},
         QObject::tr("D-Pad Down / Left Stick Down")},
        {QStringLiteral("nav.left"),
         QObject::tr("Move left"),
         QStringLiteral("Navigation"),
         QString::fromLatin1(InputMap::kContextBrowse),
         {QStringLiteral("Left")},
         QObject::tr("D-Pad Left / Left Stick Left")},
        {QStringLiteral("nav.right"),
         QObject::tr("Move right"),
         QStringLiteral("Navigation"),
         QString::fromLatin1(InputMap::kContextBrowse),
         {QStringLiteral("Right")},
         QObject::tr("D-Pad Right / Left Stick Right")},
        {QStringLiteral("nav.select"),
         QObject::tr("Select"),
         QStringLiteral("Navigation"),
         QString::fromLatin1(InputMap::kContextBrowse),
         {QStringLiteral("Return"), QStringLiteral("Enter"), QStringLiteral("Space")},
         QObject::tr("A")},
        // Main.qml Keys.onEscapePressed / Keys.onBackPressed.
        {QStringLiteral("nav.back"),
         QObject::tr("Back"),
         QStringLiteral("Navigation"),
         QString::fromLatin1(InputMap::kContextBrowse),
         {QStringLiteral("Esc"), QStringLiteral("Backspace")},
         QObject::tr("B")},

        // Library — Main.qml Shortcut { sequence: "/" }.
        {QStringLiteral("nav.pageUp"),
         QObject::tr("Page up"),
         QStringLiteral("Navigation"),
         QString::fromLatin1(InputMap::kContextBrowse),
         {QStringLiteral("PgUp")},
         QObject::tr("LT")},
        {QStringLiteral("nav.pageDown"),
         QObject::tr("Page down"),
         QStringLiteral("Navigation"),
         QString::fromLatin1(InputMap::kContextBrowse),
         {QStringLiteral("PgDown")},
         QObject::tr("RT")},
        {QStringLiteral("nav.previousTab"),
         QObject::tr("Previous library"),
         QStringLiteral("Navigation"),
         QString::fromLatin1(InputMap::kContextBrowse),
         {QStringLiteral("Ctrl+Shift+Tab")},
         QObject::tr("LB")},
        {QStringLiteral("nav.nextTab"),
         QObject::tr("Next library"),
         QStringLiteral("Navigation"),
         QString::fromLatin1(InputMap::kContextBrowse),
         {QStringLiteral("Ctrl+Tab")},
         QObject::tr("RB")},
        {QStringLiteral("app.toggleMenu"),
         QObject::tr("Show / hide the menu"),
         QStringLiteral("Application"),
         QString::fromLatin1(InputMap::kContextGlobal),
         {QStringLiteral("M")},
         QObject::tr("Menu")},
        {QStringLiteral("library.search"),
         QObject::tr("Search"),
         QStringLiteral("Library"),
         QString::fromLatin1(InputMap::kContextGlobal),
         {QStringLiteral("/")},
         QString()},

        // Application — Main.qml Shortcut "F2", "F11" and "F".
        {QStringLiteral("app.settings"),
         QObject::tr("Settings"),
         QStringLiteral("Application"),
         QString::fromLatin1(InputMap::kContextGlobal),
         {QStringLiteral("F2")},
         QString()},
        {QStringLiteral("app.fullscreen"),
         QObject::tr("Toggle full screen"),
         QStringLiteral("Application"),
         QString::fromLatin1(InputMap::kContextGlobal),
         {QStringLiteral("F11"), QStringLiteral("F")},
         QString()},
        // Chrome added by the M9 shell wave; Main.qml resolves these through the
        // map, so listing them here is what makes them remappable and what puts
        // them in the shortcut sheet.
        {QStringLiteral("app.shortcuts"),
         QObject::tr("Keyboard shortcuts"),
         QStringLiteral("Application"),
         QString::fromLatin1(InputMap::kContextGlobal),
         {QStringLiteral("?")},
         QString()},
        {QStringLiteral("app.commandPalette"),
         QObject::tr("Command palette"),
         QStringLiteral("Application"),
         QString::fromLatin1(InputMap::kContextGlobal),
         {QStringLiteral("Ctrl+K")},
         QString()},

        // Playback — PlayerPage.qml Keys.onPressed.
        {QStringLiteral("player.togglePause"),
         QObject::tr("Play / Pause"),
         QStringLiteral("Playback"),
         QString::fromLatin1(InputMap::kContextPlayer),
         {QStringLiteral("Space"), QStringLiteral("K")},
         QObject::tr("A")},
        // Plain Left/Right are NOT here any more: on the player page they are
        // navigation, and the scrubber owns them once armed (StrmSlider
        // armToScrub). A direction that seeks merely because nothing is focused
        // yet is how a film jumped while the user was still finding the
        // controls. J/L — the convention every video site teaches — are the
        // discrete jumps, and LT/RT resolve through them.
        {QStringLiteral("player.seekBackward"),
         QObject::tr("Seek back 10 seconds"),
         QStringLiteral("Playback"),
         QString::fromLatin1(InputMap::kContextPlayer),
         {QStringLiteral("J")},
         QObject::tr("LT")},
        {QStringLiteral("player.seekForward"),
         QObject::tr("Seek forward 10 seconds"),
         QStringLiteral("Playback"),
         QString::fromLatin1(InputMap::kContextPlayer),
         {QStringLiteral("L")},
         QObject::tr("RT")},
        // GamepadManager maps LB→PgDown and RB→PgUp, i.e. the 60 s jumps.
        {QStringLiteral("player.seekBackwardLong"),
         QObject::tr("Seek back 60 seconds"),
         QStringLiteral("Playback"),
         QString::fromLatin1(InputMap::kContextPlayer),
         {QStringLiteral("PgDown")},
         QObject::tr("LB")},
        {QStringLiteral("player.seekForwardLong"),
         QObject::tr("Seek forward 60 seconds"),
         QStringLiteral("Playback"),
         QString::fromLatin1(InputMap::kContextPlayer),
         {QStringLiteral("PgUp")},
         QObject::tr("RB")},
        {QStringLiteral("player.cycleAudio"),
         QObject::tr("Next audio track"),
         QStringLiteral("Playback"),
         QString::fromLatin1(InputMap::kContextPlayer),
         {QStringLiteral("A")},
         QObject::tr("X")},
        {QStringLiteral("player.cycleSubtitle"),
         QObject::tr("Next subtitle track"),
         QStringLiteral("Playback"),
         QString::fromLatin1(InputMap::kContextPlayer),
         {QStringLiteral("C")},
         QObject::tr("Y")},
        {QStringLiteral("player.toggleOsd"),
         QObject::tr("Show / hide the on-screen display"),
         QStringLiteral("Playback"),
         QString::fromLatin1(InputMap::kContextPlayer),
         {QStringLiteral("I")},
         QObject::tr("Menu")},
        {QStringLiteral("player.frameNext"),
         QObject::tr("Step one frame forward"),
         QStringLiteral("Playback"),
         QString::fromLatin1(InputMap::kContextPlayer),
         {QStringLiteral(".")},
         QString()},
        {QStringLiteral("player.framePrevious"),
         QObject::tr("Step one frame back"),
         QStringLiteral("Playback"),
         QString::fromLatin1(InputMap::kContextPlayer),
         {QStringLiteral(",")},
         QString()},
        {QStringLiteral("player.screenshot"),
         QObject::tr("Save a screenshot"),
         QStringLiteral("Playback"),
         QString::fromLatin1(InputMap::kContextPlayer),
         {QStringLiteral("Ctrl+S")},
         QString()},
        {QStringLiteral("player.markLoop"),
         QObject::tr("Mark A–B loop point"),
         QStringLiteral("Playback"),
         QString::fromLatin1(InputMap::kContextPlayer),
         // B as in A–B; L moved to seek-forward when the arrows stopped
         // seeking (see player.seekForward above).
         {QStringLiteral("B")},
         QString()},
        // Esc lives here, not on player.stop. It is the key every other
        // application spells "go back", and ending a record with it — losing
        // the queue, the shuffle and the position — is not what anybody who
        // pressed it meant. The gamepad's Back button goes with it for the
        // same reason. Stop keeps a key of its own, and now has a button.
        {QStringLiteral("player.minimize"),
         QObject::tr("Leave the player, keep playing"),
         QStringLiteral("Playback"),
         QString::fromLatin1(InputMap::kContextPlayer),
         {QStringLiteral("Backspace"), QStringLiteral("Esc"), QStringLiteral("Back")},
         QObject::tr("View")},
        {QStringLiteral("player.stop"),
         QObject::tr("Stop playback"),
         QStringLiteral("Playback"),
         QString::fromLatin1(InputMap::kContextPlayer),
         {QStringLiteral("S")},
         QString()},
        // PlayerPage and the now-playing panel handle these; on a pad the
        // right stick's vertical axis repeats them (GamepadManager), which is
        // the one volume path a pad has — the slider only takes pointer focus.
        {QStringLiteral("player.volumeUp"),
         QObject::tr("Volume up"),
         QStringLiteral("Playback"),
         QString::fromLatin1(InputMap::kContextPlayer),
         {QStringLiteral("+")},
         QObject::tr("Right Stick Up")},
        {QStringLiteral("player.volumeDown"),
         QObject::tr("Volume down"),
         QStringLiteral("Playback"),
         QString::fromLatin1(InputMap::kContextPlayer),
         {QStringLiteral("-")},
         QObject::tr("Right Stick Down")},

        // ── The docked bar, from the keyboard and from a pad ──────────────
        // MiniPlayer::focusTransport() has existed since the bar did and
        // nothing called it: the strip could be reached by Tab or by clicking
        // it, which is to say a gamepad could not reach it at all. Browse
        // context, not player: the bar exists precisely while the player page
        // is NOT on top.
        //
        // "N" for now-playing. R3 because every other button on the pad is
        // already spoken for in browse context, and clicking the right stick is
        // the one gesture nothing else in this app uses.
        {QStringLiteral("player.focusBar"),
         QObject::tr("Jump to the now-playing bar"),
         QStringLiteral("Playback"),
         QString::fromLatin1(InputMap::kContextBrowse),
         {QStringLiteral("N")},
         QObject::tr("R3 (Right Stick click)")},

        // The short way between the film and the docked bar: leaving the
        // player keeps it playing, and getting back without this is the whole
        // navigation stack. Global context — the verb exists in both states,
        // minimizing when the page is on top and expanding when it is not
        // (Main.qml's MappedShortcut decides which).
        {QStringLiteral("player.toggleView"),
         QObject::tr("Full player / mini player"),
         QStringLiteral("Playback"),
         QString::fromLatin1(InputMap::kContextGlobal),
         {QStringLiteral("V")},
         QObject::tr("L3 (Left Stick click)")},

        // ── Music (MUSIC.md §7) ───────────────────────────────────────────
        // Its own context, because each of these three keys is already bound in
        // browse or in player and only a non-overlapping context lets music
        // have them. See InputMap::kContextMusic.
        //
        // A coexistence note that is not obvious: TrackTable claims single
        // printable characters through Keys.onShortcutOverride while it has
        // focus and type-to-jump is on, so "S" and "L" typed into a track table
        // jump to a song rather than firing these. That is correct — the user
        // is typing — and it is why the pages arm these on the page rather than
        // on the table. Space is exempt at the table too until a word is
        // already being typed, so play/pause works from a track list.
        {QStringLiteral("music.playPause"),
         QObject::tr("Play / pause while browsing"),
         QStringLiteral("Music"),
         QString::fromLatin1(InputMap::kContextMusic),
         {QStringLiteral("Space")},
         QString()},
        {QStringLiteral("music.shuffleAll"),
         QObject::tr("Shuffle this music library"),
         QStringLiteral("Music"),
         QString::fromLatin1(InputMap::kContextMusic),
         {QStringLiteral("S")},
         QString()},
        {QStringLiteral("music.favorite"),
         QObject::tr("Favourite what is selected"),
         QStringLiteral("Music"),
         QString::fromLatin1(InputMap::kContextMusic),
         {QStringLiteral("L")},
         QString()},
        {QStringLiteral("music.instantMix"),
         QObject::tr("Instant mix from here"),
         QStringLiteral("Music"),
         QString::fromLatin1(InputMap::kContextMusic),
         {QStringLiteral("R")},
         QString()},
    };
    return defs;
}

bool isKnownDevice(const QString &device)
{
    return device == QLatin1String("keyboard") || device == QLatin1String("mouse") ||
           device == QLatin1String("gamepad");
}

} // namespace

InputMap::InputMap(QObject *parent) : QObject(parent)
{
    load();
}

InputMap::InputMap(const QString &iniFilePath, QObject *parent)
    : QObject(parent), m_store(iniFilePath, QSettings::IniFormat)
{
    load();
}

void InputMap::load()
{
    const QString device = m_store.value(kLastDeviceKey).toString();
    if (isKnownDevice(device))
        m_lastInputDevice = device;

    // Two passes, and the split matters. conflictFor() answers from bindings(),
    // which reports a default for every action whose override has not been
    // inserted yet — so detecting conflicts while loading judges a stored
    // binding against defaults the user already replaced, and the second half
    // of a valid swap (app.settings -> Ctrl+, plus library.search -> F2) reads
    // as a collision with the default it just freed. Load every override
    // first; only then is the set conflictFor() sees the one the user has.
    for (const ActionDef &def : catalogue()) {
        const QVariant stored = m_store.value(kOverrideGroup + def.id);
        if (!stored.isValid())
            continue;
        QStringList sequences;
        for (const QString &raw : stored.toStringList()) {
            const QString normalised = normalizeSequence(raw);
            if (normalised.isEmpty()) {
                qCWarning(logApp) << "input map: dropping unparseable stored binding" << raw
                                  << "for" << def.id;
                continue;
            }
            sequences.append(normalised);
        }
        if (sequences.isEmpty() || sequences == def.defaultSequences) {
            m_store.remove(kOverrideGroup + def.id);
            continue;
        }
        m_overrides.insert(def.id, sequences);
    }

    // A stored binding can collide with a default that changed between
    // releases; the default wins and the stale override is dropped. Dropping
    // one is visible to the checks that follow, so a stored file that somehow
    // holds two overrides on the same sequence keeps the first in catalogue
    // order rather than discarding both.
    for (const ActionDef &def : catalogue()) {
        const auto it = m_overrides.constFind(def.id);
        if (it == m_overrides.cend())
            continue;
        const QStringList sequences = *it;
        for (const QString &sequence : sequences) {
            const QString other = conflictFor(def.id, sequence);
            if (other.isEmpty())
                continue;
            qCWarning(logApp) << "input map: stored binding" << sequence << "for" << def.id
                              << "conflicts with" << other << "— keeping the default";
            // Drop the stale override from the store as well. Leaving it there
            // means the same warning on every launch for the rest of the
            // installation's life, and a "custom" binding the remap UI can
            // never show.
            m_overrides.remove(def.id);
            m_store.remove(kOverrideGroup + def.id);
            break;
        }
    }
}

void InputMap::store(const QString &actionId)
{
    const auto it = m_overrides.constFind(actionId);
    if (it == m_overrides.cend())
        m_store.remove(kOverrideGroup + actionId);
    else
        m_store.setValue(kOverrideGroup + actionId, *it);
}

const InputMap::ActionDef *InputMap::definition(const QString &actionId) const
{
    for (const ActionDef &def : catalogue()) {
        if (def.id == actionId)
            return &def;
    }
    return nullptr;
}

QVariantMap InputMap::describe(const ActionDef &def) const
{
    const QStringList current = bindings(def.id);
    QVariantMap map;
    map.insert(QStringLiteral("actionId"), def.id);
    map.insert(QStringLiteral("name"), def.name);
    map.insert(QStringLiteral("category"), def.category);
    map.insert(QStringLiteral("context"), def.context);
    map.insert(QStringLiteral("sequence"), current.value(0));
    map.insert(QStringLiteral("sequences"), current);
    map.insert(QStringLiteral("defaultSequence"), def.defaultSequences.value(0));
    map.insert(QStringLiteral("defaultSequences"), def.defaultSequences);
    map.insert(QStringLiteral("gamepad"), def.gamepad);
    map.insert(QStringLiteral("custom"), m_overrides.contains(def.id));
    return map;
}

QVariantList InputMap::actions() const
{
    QVariantList list;
    list.reserve(catalogue().size());
    for (const ActionDef &def : catalogue())
        list.append(describe(def));
    return list;
}

QVariantList InputMap::actionsForCategory(const QString &category) const
{
    QVariantList list;
    for (const ActionDef &def : catalogue()) {
        if (def.category == category)
            list.append(describe(def));
    }
    return list;
}

QStringList InputMap::categories() const
{
    QStringList out;
    for (const ActionDef &def : catalogue()) {
        if (!out.contains(def.category))
            out.append(def.category);
    }
    return out;
}

QStringList InputMap::actionIds() const
{
    QStringList out;
    out.reserve(catalogue().size());
    for (const ActionDef &def : catalogue())
        out.append(def.id);
    return out;
}

bool InputMap::hasAction(const QString &actionId) const
{
    return definition(actionId) != nullptr;
}

QString InputMap::binding(const QString &actionId) const
{
    return bindings(actionId).value(0);
}

QStringList InputMap::bindings(const QString &actionId) const
{
    const auto it = m_overrides.constFind(actionId);
    if (it != m_overrides.cend())
        return *it;
    const ActionDef *def = definition(actionId);
    return def ? def->defaultSequences : QStringList();
}

QString InputMap::defaultBinding(const QString &actionId) const
{
    return defaultBindings(actionId).value(0);
}

QStringList InputMap::defaultBindings(const QString &actionId) const
{
    const ActionDef *def = definition(actionId);
    return def ? def->defaultSequences : QStringList();
}

QString InputMap::gamepadBinding(const QString &actionId) const
{
    const ActionDef *def = definition(actionId);
    return def ? def->gamepad : QString();
}

QString InputMap::displayName(const QString &actionId) const
{
    const ActionDef *def = definition(actionId);
    return def ? def->name : QString();
}

QString InputMap::category(const QString &actionId) const
{
    const ActionDef *def = definition(actionId);
    return def ? def->category : QString();
}

QString InputMap::context(const QString &actionId) const
{
    const ActionDef *def = definition(actionId);
    return def ? def->context : QString();
}

bool InputMap::isCustomised(const QString &actionId) const
{
    return m_overrides.contains(actionId);
}

bool InputMap::contextsOverlap(const QString &a, const QString &b)
{
    return a == b || a == QLatin1String(kContextGlobal) || b == QLatin1String(kContextGlobal);
}

QString InputMap::conflictFor(const QString &actionId, const QString &sequence) const
{
    const ActionDef *self = definition(actionId);
    if (!self)
        return {};
    for (const ActionDef &def : catalogue()) {
        if (def.id == actionId)
            continue;
        if (!contextsOverlap(self->context, def.context))
            continue;
        if (bindings(def.id).contains(sequence))
            return def.id;
    }
    return {};
}

bool InputMap::setBinding(const QString &actionId, const QString &sequence)
{
    return setBindings(actionId, {sequence});
}

bool InputMap::setBindings(const QString &actionId, const QStringList &sequences)
{
    const ActionDef *def = definition(actionId);
    if (!def) {
        qCWarning(logApp) << "input map: unknown action" << actionId;
        return false;
    }

    QStringList normalised;
    for (const QString &raw : sequences) {
        const QString sequence = normalizeSequence(raw);
        if (sequence.isEmpty()) {
            qCWarning(logApp) << "input map: rejecting unparseable sequence" << raw << "for"
                              << actionId;
            emit bindingConflict(actionId, raw, QString());
            return false;
        }
        if (normalised.contains(sequence))
            continue;
        normalised.append(sequence);
    }
    if (normalised.isEmpty()) {
        emit bindingConflict(actionId, QString(), QString());
        return false;
    }

    // Reject the whole call on the first conflict — the previous binding stands.
    for (const QString &sequence : std::as_const(normalised)) {
        const QString other = conflictFor(actionId, sequence);
        if (!other.isEmpty()) {
            qCInfo(logApp) << "input map:" << sequence << "is already bound to" << other;
            emit bindingConflict(actionId, sequence, other);
            return false;
        }
    }

    if (normalised == bindings(actionId))
        return true;

    if (normalised == def->defaultSequences)
        m_overrides.remove(actionId);
    else
        m_overrides.insert(actionId, normalised);
    store(actionId);
    emit bindingChanged(actionId, normalised.value(0));
    emit actionsChanged();
    return true;
}

bool InputMap::resetBinding(const QString &actionId)
{
    const ActionDef *def = definition(actionId);
    if (!def)
        return false;
    if (m_overrides.remove(actionId) == 0)
        return true;
    store(actionId);
    emit bindingChanged(actionId, def->defaultSequences.value(0));
    emit actionsChanged();
    return true;
}

void InputMap::resetAll()
{
    if (m_overrides.isEmpty())
        return;
    const QStringList changed = m_overrides.keys();
    m_overrides.clear();
    for (const QString &actionId : changed) {
        store(actionId);
        emit bindingChanged(actionId, defaultBinding(actionId));
    }
    emit actionsChanged();
}

QString InputMap::actionForSequence(const QString &sequence, const QString &context) const
{
    const QString needle = normalizeSequence(sequence);
    if (needle.isEmpty())
        return {};
    for (const ActionDef &def : catalogue()) {
        if (!context.isEmpty() && !contextsOverlap(context, def.context))
            continue;
        if (bindings(def.id).contains(needle))
            return def.id;
    }
    return {};
}

QString InputMap::actionForKey(int key, int modifiers, const QString &context) const
{
    return actionForSequence(sequenceForKey(key, modifiers), context);
}

int InputMap::keyFor(const QString &actionId) const
{
    return parseSequence(binding(actionId)).key;
}

int InputMap::modifiersFor(const QString &actionId) const
{
    return parseSequence(binding(actionId)).modifiers;
}

QString InputMap::normalizeSequence(const QString &sequence) const
{
    const ParsedSequence parsed = parseSequence(sequence);
    if (!parsed.valid)
        return {};
    return formatSequence(parsed.key, parsed.modifiers);
}

QString InputMap::sequenceForKey(int key, int modifiers) const
{
    // Keypad/auto-repeat flags carry no meaning for a binding.
    return formatSequence(key, modifiers & (Qt::ControlModifier | Qt::AltModifier |
                                            Qt::ShiftModifier | Qt::MetaModifier));
}

bool InputMap::isTypableSequence(const QString &sequence) const
{
    const ParsedSequence parsed = parseSequence(sequence);
    if (!parsed.valid)
        return false;
    // Shift is part of typing — a capital, a symbol, a shifted range extension.
    // Every other modifier makes a chord, and a chord is exactly what must keep
    // working while a field has focus.
    if (parsed.modifiers & ~int(Qt::ShiftModifier))
        return false;
    return isTypableKey(parsed.key);
}

void InputMap::noteInput(const QString &device)
{
    if (!isKnownDevice(device)) {
        qCWarning(logApp) << "input map: ignoring unknown input device" << device;
        return;
    }
    if (device == m_lastInputDevice)
        return;
    m_lastInputDevice = device;
    m_store.setValue(kLastDeviceKey, device);
    emit lastInputDeviceChanged();
}

} // namespace strmqt
