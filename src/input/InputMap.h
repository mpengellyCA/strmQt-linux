#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace strmqt {

// One source of truth for every binding (PLAN §3.7, ARCHITECTURE.md): feeds the
// shortcut sheet, the remap UI and every page, instead of shortcuts being
// hardcoded per page.
//
// Layering: this lives in strmqt_app, which is QtCore/QtNetwork/QtDBus only —
// no QtGui, so no QKeySequence. Sequences are therefore plain strings in the
// spelling QKeySequence::fromString() accepts ("Space", "PgDown", "Ctrl+K"),
// normalised here, and handed to QML `Shortcut { sequences: ... }` untouched.
// Qt::Key / Qt::KeyboardModifier are QtCore, so key-code lookups are available
// for `Keys.onPressed` handlers that switch on event.key.
//
// Media keys (PLAN §3.7 "Space / Media", "S / Media") are deliberately absent:
// on the target platform they arrive through MPRIS, which Application already
// wires to PlayerController, not through the Qt shortcut system.
class InputMap : public QObject
{
    Q_OBJECT
    // Whole catalogue as a QML list model: one map per action (see actions()).
    Q_PROPERTY(QVariantList actions READ actions NOTIFY actionsChanged)
    // "keyboard" | "mouse" | "gamepad" — drives the TV-density switch (DESIGN F3).
    Q_PROPERTY(QString lastInputDevice READ lastInputDevice NOTIFY lastInputDeviceChanged)
    Q_PROPERTY(bool gamepadActive READ gamepadActive NOTIFY lastInputDeviceChanged)

public:
    // Contexts a binding is live in. Two actions only conflict when their
    // contexts overlap, which is what lets Space mean Select while browsing and
    // Play/Pause in the player — exactly as the pages behave today.
    static constexpr auto kContextGlobal = "global";
    static constexpr auto kContextBrowse = "browse";
    static constexpr auto kContextPlayer = "player";
    // A third context (MUSIC.md §7): the music library, an album and an artist
    // page. It exists for the keys that mean something different while music is
    // what is on screen — Space is play/pause for the docked bar rather than
    // Select, "S" shuffles the library rather than stopping the player, "L"
    // favourites the row under the cursor rather than marking a loop point.
    //
    // Non-overlapping with browse AND with player, which is the whole point:
    // each of those three keys is already bound in one of the other two, and
    // only a context of its own lets music have them without a conflict. The
    // pages arm their shortcuts on their own `visible`, so "music context" is
    // "a music page is the one on screen" and nothing has to track it centrally.
    static constexpr auto kContextMusic = "music";

    struct ActionDef
    {
        QString id;
        QString name;     // translated display name
        QString category; // "Navigation" | "Playback" | "Library" | "Application"
        QString context;  // kContext*
        QStringList defaultSequences;
        QString gamepad; // human-readable default gamepad binding, may be empty
    };

    explicit InputMap(QObject *parent = nullptr);
    // Test constructor: back the override store with an explicit INI file
    // instead of the platform-default location (mirrors strmqt::Settings).
    explicit InputMap(const QString &iniFilePath, QObject *parent = nullptr);

    // One QVariantMap per action:
    //   actionId, name, category, context, sequence (primary), sequences,
    //   defaultSequence, defaultSequences, gamepad, custom (bool)
    Q_INVOKABLE QVariantList actions() const;
    Q_INVOKABLE QVariantList actionsForCategory(const QString &category) const;
    Q_INVOKABLE QStringList categories() const;
    Q_INVOKABLE QStringList actionIds() const;
    Q_INVOKABLE bool hasAction(const QString &actionId) const;

    // Primary sequence, ready for `Shortcut { sequence: Input.binding(id) }`.
    Q_INVOKABLE QString binding(const QString &actionId) const;
    // Every sequence, for `Shortcut { sequences: Input.bindings(id) }`.
    Q_INVOKABLE QStringList bindings(const QString &actionId) const;
    Q_INVOKABLE QString defaultBinding(const QString &actionId) const;
    Q_INVOKABLE QStringList defaultBindings(const QString &actionId) const;
    Q_INVOKABLE QString gamepadBinding(const QString &actionId) const;
    Q_INVOKABLE QString displayName(const QString &actionId) const;
    Q_INVOKABLE QString category(const QString &actionId) const;
    Q_INVOKABLE QString context(const QString &actionId) const;
    Q_INVOKABLE bool isCustomised(const QString &actionId) const;

    // Rebinding. Rejects (returns false, leaves the old binding in place, emits
    // bindingConflict) a sequence already bound to another action whose context
    // overlaps. An unparseable or empty sequence is rejected the same way.
    Q_INVOKABLE bool setBinding(const QString &actionId, const QString &sequence);
    Q_INVOKABLE bool setBindings(const QString &actionId, const QStringList &sequences);
    Q_INVOKABLE bool resetBinding(const QString &actionId);
    Q_INVOKABLE void resetAll();

    // Reverse lookups for `Keys.onPressed` handlers. An empty context matches
    // any action; otherwise global actions plus that context's actions.
    Q_INVOKABLE QString actionForSequence(const QString &sequence,
                                          const QString &context = QString()) const;
    Q_INVOKABLE QString actionForKey(int key, int modifiers = 0,
                                     const QString &context = QString()) const;
    // Qt::Key of the primary binding, 0 when it does not resolve to one key.
    Q_INVOKABLE int keyFor(const QString &actionId) const;
    // Qt::KeyboardModifiers of the primary binding.
    Q_INVOKABLE int modifiersFor(const QString &actionId) const;

    // Canonicalises user input from the remap UI ("ctrl+k" → "Ctrl+K"); empty
    // when the string does not parse.
    Q_INVOKABLE QString normalizeSequence(const QString &sequence) const;
    Q_INVOKABLE QString sequenceForKey(int key, int modifiers = 0) const;

    QString lastInputDevice() const { return m_lastInputDevice; }
    bool gamepadActive() const { return m_lastInputDevice == QLatin1String("gamepad"); }
    // Reported by the UI on every real input event; unknown devices are ignored.
    Q_INVOKABLE void noteInput(const QString &device);

signals:
    void actionsChanged();
    void bindingChanged(const QString &actionId, const QString &sequence);
    void bindingConflict(const QString &actionId, const QString &sequence,
                         const QString &conflictingActionId);
    void lastInputDeviceChanged();

private:
    void load();
    void store(const QString &actionId);
    const ActionDef *definition(const QString &actionId) const;
    QVariantMap describe(const ActionDef &def) const;
    // Returns the id of an action other than actionId already holding sequence
    // in an overlapping context, or an empty string.
    QString conflictFor(const QString &actionId, const QString &sequence) const;
    static bool contextsOverlap(const QString &a, const QString &b);

    QSettings m_store;
    QHash<QString, QStringList> m_overrides; // actionId → current sequences
    QString m_lastInputDevice = QStringLiteral("keyboard");
};

} // namespace strmqt
