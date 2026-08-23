#include <QSignalSpy>
#include <QtTest>

#include "input/InputMap.h"

using strmqt::InputMap;

class InputMapTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void defaultsMatchTodaysBindings();
    void defaultsHaveNoConflicts();
    void planSection37IsFullyCovered();
    void customBindingPersistsAndReloads();
    void conflictingBindingIsRejected();
    void contextsKeepBrowseAndPlayerApart();
    void resetRestoresTheDefault();
    void resetAllClearsEveryOverride();
    void sequencesAreNormalised();
    void keyLookupsResolveActions();
    void lastInputDeviceTracksTheUser();

private:
    QString ini() const { return m_dir->filePath(QStringLiteral("input.ini")); }

    QTemporaryDir *m_dir = nullptr;
    InputMap *m_map = nullptr;
};

void InputMapTest::init()
{
    m_dir = new QTemporaryDir;
    QVERIFY(m_dir->isValid());
    m_map = new InputMap(ini(), this);
}

void InputMapTest::cleanup()
{
    delete m_map;
    m_map = nullptr;
    delete m_dir;
    m_dir = nullptr;
}

// Every expectation below is the binding the app honours today: Main.qml for the
// application/navigation rows, PlayerPage.qml for the playback rows.
void InputMapTest::defaultsMatchTodaysBindings()
{
    // Main.qml
    QCOMPARE(m_map->bindings(QStringLiteral("library.search")), QStringList{QStringLiteral("/")});
    QCOMPARE(m_map->bindings(QStringLiteral("app.settings")), QStringList{QStringLiteral("F2")});
    QCOMPARE(m_map->bindings(QStringLiteral("app.fullscreen")),
             (QStringList{QStringLiteral("F11"), QStringLiteral("F")}));
    QCOMPARE(m_map->bindings(QStringLiteral("nav.back")),
             (QStringList{QStringLiteral("Esc"), QStringLiteral("Backspace")}));

    // PlayerPage.qml
    QCOMPARE(m_map->bindings(QStringLiteral("player.togglePause")),
             (QStringList{QStringLiteral("Space"), QStringLiteral("K")}));
    QCOMPARE(m_map->bindings(QStringLiteral("player.seekBackward")),
             QStringList{QStringLiteral("Left")});
    QCOMPARE(m_map->bindings(QStringLiteral("player.seekForward")),
             QStringList{QStringLiteral("Right")});
    QCOMPARE(m_map->bindings(QStringLiteral("player.seekBackwardLong")),
             QStringList{QStringLiteral("PgDown")});
    QCOMPARE(m_map->bindings(QStringLiteral("player.seekForwardLong")),
             QStringList{QStringLiteral("PgUp")});
    QCOMPARE(m_map->bindings(QStringLiteral("player.cycleAudio")),
             QStringList{QStringLiteral("A")});
    QCOMPARE(m_map->bindings(QStringLiteral("player.cycleSubtitle")),
             QStringList{QStringLiteral("C")});
    QCOMPARE(m_map->bindings(QStringLiteral("player.toggleOsd")), QStringList{QStringLiteral("I")});
    QCOMPARE(m_map->bindings(QStringLiteral("player.stop")),
             (QStringList{QStringLiteral("S"), QStringLiteral("Esc"), QStringLiteral("Back")}));

    // The catalogue is what the shortcut sheet and the remap UI render.
    const QVariantList actions = m_map->actions();
    QCOMPARE(actions.size(), m_map->actionIds().size());
    const QVariantMap first = actions.first().toMap();
    for (const QString &key :
         {QStringLiteral("actionId"), QStringLiteral("name"), QStringLiteral("category"),
          QStringLiteral("context"), QStringLiteral("sequence"), QStringLiteral("sequences"),
          QStringLiteral("defaultSequence"), QStringLiteral("gamepad"), QStringLiteral("custom")})
        QVERIFY2(first.contains(key), qPrintable(key));

    const QStringList categories = m_map->categories();
    for (const QString &category : {QStringLiteral("Navigation"), QStringLiteral("Playback"),
                                    QStringLiteral("Library"), QStringLiteral("Application")}) {
        QVERIFY2(categories.contains(category), qPrintable(category));
        QVERIFY(!m_map->actionsForCategory(category).isEmpty());
    }
}

// Two actions may only share a sequence when their contexts cannot both be live.
void InputMapTest::defaultsHaveNoConflicts()
{
    const auto overlaps = [](const QString &a, const QString &b) {
        return a == b || a == QLatin1String("global") || b == QLatin1String("global");
    };
    const QStringList ids = m_map->actionIds();
    for (const QString &id : ids) {
        const QStringList sequences = m_map->bindings(id);
        QVERIFY2(!sequences.isEmpty(), qPrintable(id));
        for (const QString &sequence : sequences)
            QVERIFY2(!sequence.isEmpty(), qPrintable(id));

        for (const QString &other : ids) {
            if (other == id || !overlaps(m_map->context(id), m_map->context(other)))
                continue;
            for (const QString &sequence : sequences) {
                QVERIFY2(
                    !m_map->bindings(other).contains(sequence),
                    qPrintable(QStringLiteral("%1 and %2 both bind %3").arg(id, other, sequence)));
            }
        }
        // Rebinding an action to the sequence it already owns is a no-op, never
        // a self-conflict.
        QVERIFY2(m_map->setBindings(id, m_map->bindings(id)), qPrintable(id));
        QVERIFY2(!m_map->isCustomised(id), qPrintable(id));
    }

    // Same sequence, different (non-overlapping) contexts is legal and used.
    QCOMPARE(m_map->context(QStringLiteral("nav.select")), QStringLiteral("browse"));
    QCOMPARE(m_map->context(QStringLiteral("player.togglePause")), QStringLiteral("player"));
    QVERIFY(m_map->bindings(QStringLiteral("nav.select")).contains(QStringLiteral("Space")));
    QVERIFY(
        m_map->bindings(QStringLiteral("player.togglePause")).contains(QStringLiteral("Space")));
}

// ARCHITECTURE.md lists nine rows; every one has an action with a keyboard binding,
// and every row that names a gamepad button has one too.
void InputMapTest::planSection37IsFullyCovered()
{
    const QStringList keyboardRows = {
        QStringLiteral("nav.up"),
        QStringLiteral("nav.down"),
        QStringLiteral("nav.left"),
        QStringLiteral("nav.right"),
        QStringLiteral("nav.select"),
        QStringLiteral("nav.back"),
        QStringLiteral("player.togglePause"),
        QStringLiteral("player.seekBackward"),
        QStringLiteral("player.seekForward"),
        QStringLiteral("player.seekBackwardLong"),
        QStringLiteral("player.seekForwardLong"),
        QStringLiteral("player.stop"),
        QStringLiteral("app.fullscreen"),
        QStringLiteral("player.toggleOsd"),
        QStringLiteral("player.volumeUp"),
        QStringLiteral("player.volumeDown"),
    };
    for (const QString &id : keyboardRows) {
        QVERIFY2(m_map->hasAction(id), qPrintable(id));
        QVERIFY2(!m_map->binding(id).isEmpty(), qPrintable(id));
        QVERIFY2(!m_map->displayName(id).isEmpty(), qPrintable(id));
    }

    // Rows PLAN §3.7 gives a gamepad binding.
    //
    // player.volumeUp/Down are deliberately NOT here. They advertised "Right
    // Stick Up/Down" while GamepadManager never read the right stick and
    // PlayerController has no volume verb, so the hint described a control that
    // did nothing. A binding is documented once it works, not before.
    const QStringList gamepadRows = {
        QStringLiteral("nav.up"),
        QStringLiteral("nav.select"),
        QStringLiteral("nav.back"),
        QStringLiteral("nav.nextTab"),
        QStringLiteral("nav.previousTab"),
        QStringLiteral("nav.pageUp"),
        QStringLiteral("nav.pageDown"),
        QStringLiteral("app.toggleMenu"),
        QStringLiteral("player.togglePause"),
        QStringLiteral("player.seekForward"),
        QStringLiteral("player.seekForwardLong"),
        QStringLiteral("player.stop"),
        QStringLiteral("player.toggleOsd"),
    };
    for (const QString &id : gamepadRows)
        QVERIFY2(!m_map->gamepadBinding(id).isEmpty(), qPrintable(id));

    // The invariant that keeps the hints honest, rather than a hand-kept list:
    // GamepadManager synthesizes a button press by resolving the action to its
    // bound key, so an action carrying a gamepad hint MUST resolve to exactly
    // one key. If it does not, the pad silently does nothing while the shortcut
    // sheet tells the user which button to press.
    for (const QString &id : m_map->actionIds()) {
        if (m_map->gamepadBinding(id).isEmpty())
            continue;
        QVERIFY2(m_map->keyFor(id) != 0, qPrintable(id));
    }
}

void InputMapTest::customBindingPersistsAndReloads()
{
    QSignalSpy changed(m_map, &InputMap::bindingChanged);
    QVERIFY(m_map->setBinding(QStringLiteral("app.settings"), QStringLiteral("Ctrl+,")));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.first().at(0).toString(), QStringLiteral("app.settings"));
    QCOMPARE(changed.first().at(1).toString(), QStringLiteral("Ctrl+,"));
    QCOMPARE(m_map->binding(QStringLiteral("app.settings")), QStringLiteral("Ctrl+,"));
    QVERIFY(m_map->isCustomised(QStringLiteral("app.settings")));
    QCOMPARE(m_map->defaultBinding(QStringLiteral("app.settings")), QStringLiteral("F2"));

    InputMap reloaded(ini());
    QCOMPARE(reloaded.binding(QStringLiteral("app.settings")), QStringLiteral("Ctrl+,"));
    QVERIFY(reloaded.isCustomised(QStringLiteral("app.settings")));
    // Untouched actions still read their defaults after a reload.
    QCOMPARE(reloaded.binding(QStringLiteral("library.search")), QStringLiteral("/"));
    QVERIFY(!reloaded.isCustomised(QStringLiteral("library.search")));
}

void InputMapTest::conflictingBindingIsRejected()
{
    QSignalSpy conflicts(m_map, &InputMap::bindingConflict);
    QSignalSpy changed(m_map, &InputMap::bindingChanged);

    // "/" belongs to library.search (global), so app.settings cannot steal it.
    QVERIFY(!m_map->setBinding(QStringLiteral("app.settings"), QStringLiteral("/")));
    QCOMPARE(conflicts.count(), 1);
    QCOMPARE(conflicts.first().at(0).toString(), QStringLiteral("app.settings"));
    QCOMPARE(conflicts.first().at(1).toString(), QStringLiteral("/"));
    QCOMPARE(conflicts.first().at(2).toString(), QStringLiteral("library.search"));
    QCOMPARE(changed.count(), 0);

    // The original binding survives, on this instance and after a reload.
    QCOMPARE(m_map->binding(QStringLiteral("app.settings")), QStringLiteral("F2"));
    QCOMPARE(m_map->binding(QStringLiteral("library.search")), QStringLiteral("/"));
    QVERIFY(!m_map->isCustomised(QStringLiteral("app.settings")));
    InputMap reloaded(ini());
    QCOMPARE(reloaded.binding(QStringLiteral("app.settings")), QStringLiteral("F2"));

    // Garbage is rejected too, and nothing changes.
    QVERIFY(!m_map->setBinding(QStringLiteral("app.settings"), QStringLiteral("Hyper+Nonsense")));
    QVERIFY(!m_map->setBinding(QStringLiteral("app.settings"), QString()));
    QCOMPARE(m_map->binding(QStringLiteral("app.settings")), QStringLiteral("F2"));
    // Unknown actions are rejected rather than invented.
    QVERIFY(!m_map->setBinding(QStringLiteral("nope.nothing"), QStringLiteral("F7")));
}

void InputMapTest::contextsKeepBrowseAndPlayerApart()
{
    // Esc is Back while browsing and Stop in the player: not a conflict, and a
    // rebind inside one context does not have to fight the other.
    QCOMPARE(m_map->actionForSequence(QStringLiteral("Esc"), QStringLiteral("browse")),
             QStringLiteral("nav.back"));
    QCOMPARE(m_map->actionForSequence(QStringLiteral("Esc"), QStringLiteral("player")),
             QStringLiteral("player.stop"));

    // A global action does conflict with a context-specific one.
    QVERIFY(!m_map->setBinding(QStringLiteral("app.settings"), QStringLiteral("K")));
    QVERIFY(m_map->setBinding(QStringLiteral("player.cycleAudio"), QStringLiteral("T")));
    QCOMPARE(m_map->binding(QStringLiteral("player.cycleAudio")), QStringLiteral("T"));
}

void InputMapTest::resetRestoresTheDefault()
{
    QVERIFY(m_map->setBinding(QStringLiteral("player.cycleSubtitle"), QStringLiteral("Ctrl+U")));
    QCOMPARE(m_map->binding(QStringLiteral("player.cycleSubtitle")), QStringLiteral("Ctrl+U"));

    QSignalSpy changed(m_map, &InputMap::bindingChanged);
    QVERIFY(m_map->resetBinding(QStringLiteral("player.cycleSubtitle")));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(m_map->binding(QStringLiteral("player.cycleSubtitle")), QStringLiteral("C"));
    QVERIFY(!m_map->isCustomised(QStringLiteral("player.cycleSubtitle")));

    InputMap reloaded(ini());
    QCOMPARE(reloaded.binding(QStringLiteral("player.cycleSubtitle")), QStringLiteral("C"));

    // Resetting an untouched action is a no-op, not a failure.
    QVERIFY(m_map->resetBinding(QStringLiteral("player.cycleSubtitle")));
    QVERIFY(!m_map->resetBinding(QStringLiteral("nope.nothing")));
}

void InputMapTest::resetAllClearsEveryOverride()
{
    QVERIFY(m_map->setBinding(QStringLiteral("app.settings"), QStringLiteral("Ctrl+,")));
    QVERIFY(m_map->setBinding(QStringLiteral("player.cycleAudio"), QStringLiteral("Ctrl+U")));
    m_map->resetAll();
    QCOMPARE(m_map->binding(QStringLiteral("app.settings")), QStringLiteral("F2"));
    QCOMPARE(m_map->binding(QStringLiteral("player.cycleAudio")), QStringLiteral("A"));

    InputMap reloaded(ini());
    QCOMPARE(reloaded.binding(QStringLiteral("app.settings")), QStringLiteral("F2"));
    QCOMPARE(reloaded.binding(QStringLiteral("player.cycleAudio")), QStringLiteral("A"));
}

void InputMapTest::sequencesAreNormalised()
{
    QCOMPARE(m_map->normalizeSequence(QStringLiteral("ctrl+k")), QStringLiteral("Ctrl+K"));
    QCOMPARE(m_map->normalizeSequence(QStringLiteral(" shift + ctrl + f5 ")),
             QStringLiteral("Ctrl+Shift+F5"));
    QCOMPARE(m_map->normalizeSequence(QStringLiteral("escape")), QStringLiteral("Esc"));
    QCOMPARE(m_map->normalizeSequence(QStringLiteral("PageDown")), QStringLiteral("PgDown"));
    QCOMPARE(m_map->normalizeSequence(QStringLiteral("+")), QStringLiteral("+"));
    QCOMPARE(m_map->normalizeSequence(QStringLiteral("Ctrl++")), QStringLiteral("Ctrl++"));
    QVERIFY(m_map->normalizeSequence(QStringLiteral("Ctrl+")).isEmpty());
    QVERIFY(m_map->normalizeSequence(QString()).isEmpty());
    QVERIFY(m_map->normalizeSequence(QStringLiteral("Wat+K")).isEmpty());

    // A rebind stores the canonical spelling, so conflict checks compare equal.
    QVERIFY(m_map->setBinding(QStringLiteral("app.settings"), QStringLiteral("ctrl+shift+p")));
    QCOMPARE(m_map->binding(QStringLiteral("app.settings")), QStringLiteral("Ctrl+Shift+P"));
    QVERIFY(!m_map->setBinding(QStringLiteral("library.search"), QStringLiteral("CTRL+SHIFT+P")));
}

void InputMapTest::keyLookupsResolveActions()
{
    QCOMPARE(m_map->keyFor(QStringLiteral("player.togglePause")), int(Qt::Key_Space));
    QCOMPARE(m_map->modifiersFor(QStringLiteral("player.togglePause")), 0);
    QCOMPARE(m_map->keyFor(QStringLiteral("app.settings")), int(Qt::Key_F2));
    QCOMPARE(m_map->keyFor(QStringLiteral("player.seekBackwardLong")), int(Qt::Key_PageDown));

    QCOMPARE(m_map->actionForKey(int(Qt::Key_I), 0, QStringLiteral("player")),
             QStringLiteral("player.toggleOsd"));
    QCOMPARE(m_map->actionForKey(int(Qt::Key_S), 0, QStringLiteral("player")),
             QStringLiteral("player.stop"));
    QCOMPARE(m_map->actionForKey(int(Qt::Key_F2), 0, QStringLiteral("browse")),
             QStringLiteral("app.settings"));
    QVERIFY(m_map->actionForKey(int(Qt::Key_Z), 0, QStringLiteral("player")).isEmpty());

    QVERIFY(m_map->setBinding(QStringLiteral("player.cycleAudio"), QStringLiteral("Ctrl+L")));
    QCOMPARE(m_map->keyFor(QStringLiteral("player.cycleAudio")), int(Qt::Key_L));
    QCOMPARE(m_map->modifiersFor(QStringLiteral("player.cycleAudio")), int(Qt::ControlModifier));
    QCOMPARE(
        m_map->actionForKey(int(Qt::Key_L), int(Qt::ControlModifier), QStringLiteral("player")),
        QStringLiteral("player.cycleAudio"));
}

void InputMapTest::lastInputDeviceTracksTheUser()
{
    QCOMPARE(m_map->lastInputDevice(), QStringLiteral("keyboard"));
    QVERIFY(!m_map->gamepadActive());

    QSignalSpy spy(m_map, &InputMap::lastInputDeviceChanged);
    m_map->noteInput(QStringLiteral("gamepad"));
    QCOMPARE(spy.count(), 1);
    QVERIFY(m_map->gamepadActive()); // DESIGN F3: this is the TV-density trigger
    m_map->noteInput(QStringLiteral("gamepad"));
    QCOMPARE(spy.count(), 1); // no churn while the same device keeps talking

    m_map->noteInput(QStringLiteral("telepathy")); // unknown device is ignored
    QCOMPARE(spy.count(), 1);
    QCOMPARE(m_map->lastInputDevice(), QStringLiteral("gamepad"));

    m_map->noteInput(QStringLiteral("mouse"));
    QCOMPARE(spy.count(), 2);
    QVERIFY(!m_map->gamepadActive());

    InputMap reloaded(ini());
    QCOMPARE(reloaded.lastInputDevice(), QStringLiteral("mouse"));
}

QTEST_GUILESS_MAIN(InputMapTest)
#include "tst_input_map.moc"
