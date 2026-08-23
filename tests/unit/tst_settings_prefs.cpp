#include <QTemporaryDir>
#include <QtTest>

#include "core/Settings.h"
#include "input/InputMap.h"

using strmqt::InputMap;
using strmqt::Settings;

// The appearance/volume preferences added for the M9 settings rebuild
// (ARCHITECTURE.md), plus the one invariant that is easy to break by
// accident: InputMap opens its *own* QSettings view on the same file, and the
// two must not clobber each other.
class SettingsPrefsTest : public QObject
{
    Q_OBJECT

private slots:
    void autoPlayNextEpisodeDefaultsOn();
    void densityDefaultsAndValidation();
    void themeAccentDefaultsAndValidation();
    void volumeClampsAndPersists();
    void replayGainDefaultsOffAndValidates();
    void mutePersists();
    void inputMapViewSurvivesSettingsWrites();
};

void SettingsPrefsTest::densityDefaultsAndValidation()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini = dir.filePath(QStringLiteral("test.ini"));

    {
        Settings settings(ini);
        // ARCHITECTURE.md: comfortable on the desktop.
        QCOMPARE(settings.densityMode(), QStringLiteral("comfortable"));

        QSignalSpy spy(&settings, &Settings::densityModeChanged);
        settings.setDensityMode(QStringLiteral("tv"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(settings.densityMode(), QStringLiteral("tv"));

        // Idempotent, and a value outside the vocabulary is refused rather than
        // leaving the UI at an undefined density.
        settings.setDensityMode(QStringLiteral("tv"));
        settings.setDensityMode(QStringLiteral("enormous"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(settings.densityMode(), QStringLiteral("tv"));
    }

    Settings reloaded(ini);
    QCOMPARE(reloaded.densityMode(), QStringLiteral("tv"));
    QVERIFY(Settings::densityModes().contains(QStringLiteral("compact")));

    // A hand-edited INI must not survive as a broken density.
    {
        QSettings raw(ini, QSettings::IniFormat);
        raw.setValue(QStringLiteral("appearance/density"), QStringLiteral("nonsense"));
        raw.sync();
    }
    Settings recovered(ini);
    QCOMPARE(recovered.densityMode(), QStringLiteral("comfortable"));
}

void SettingsPrefsTest::themeAccentDefaultsAndValidation()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini = dir.filePath(QStringLiteral("test.ini"));

    {
        Settings settings(ini);
        QCOMPARE(settings.themeAccent(), QStringLiteral("projection"));

        QSignalSpy spy(&settings, &Settings::themeAccentChanged);
        settings.setThemeAccent(QStringLiteral("jellyfin"));
        QCOMPARE(spy.count(), 1);
        settings.setThemeAccent(QStringLiteral("chartreuse"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(settings.themeAccent(), QStringLiteral("jellyfin"));
    }

    Settings reloaded(ini);
    QCOMPARE(reloaded.themeAccent(), QStringLiteral("jellyfin"));
    QCOMPARE(Settings::themeAccents().size(), 4);
}

void SettingsPrefsTest::volumeClampsAndPersists()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini = dir.filePath(QStringLiteral("test.ini"));

    {
        Settings settings(ini);
        QCOMPARE(settings.volume(), 100);

        QSignalSpy spy(&settings, &Settings::volumeChanged);
        settings.setVolume(65);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(settings.volume(), 65);

        // 0..130 is PlayerBackend's contract, not a suggestion.
        settings.setVolume(400);
        QCOMPARE(settings.volume(), 130);
        settings.setVolume(-20);
        QCOMPARE(settings.volume(), 0);
    }

    Settings reloaded(ini);
    QCOMPARE(reloaded.volume(), 0);

    {
        QSettings raw(ini, QSettings::IniFormat);
        raw.setValue(QStringLiteral("playback/volume"), 9999);
        raw.sync();
    }
    Settings recovered(ini);
    QCOMPARE(recovered.volume(), 130);
}

// MUSIC.md §6.3. Off by default, because a gain nobody asked for is a surprise,
// and the vocabulary is validated on the way *out* as well as in: the value is
// handed straight to mpv's `replaygain`, which rejects anything else and then
// silently keeps whatever gain the previous file left in the chain.
void SettingsPrefsTest::replayGainDefaultsOffAndValidates()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini = dir.filePath(QStringLiteral("replaygain.ini"));

    {
        Settings settings(ini);
        QCOMPARE(settings.replayGainMode(), QStringLiteral("off"));

        QSignalSpy spy(&settings, &Settings::replayGainModeChanged);
        settings.setReplayGainMode(QStringLiteral("album"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(settings.replayGainMode(), QStringLiteral("album"));

        // Idempotent, and outside the vocabulary is refused rather than stored.
        settings.setReplayGainMode(QStringLiteral("album"));
        settings.setReplayGainMode(QStringLiteral("loud"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(settings.replayGainMode(), QStringLiteral("album"));

        settings.setReplayGainMode(QStringLiteral("track"));
        QCOMPARE(spy.count(), 2);
    }

    Settings reloaded(ini);
    QCOMPARE(reloaded.replayGainMode(), QStringLiteral("track"));
    QCOMPARE(Settings::replayGainModes(),
             QStringList({QStringLiteral("off"), QStringLiteral("track"),
                          QStringLiteral("album")}));

    // A hand-edited INI must not put a value mpv will refuse into the chain.
    {
        QSettings raw(ini, QSettings::IniFormat);
        raw.setValue(QStringLiteral("playback/replayGain"), QStringLiteral("nonsense"));
        raw.sync();
    }
    Settings recovered(ini);
    QCOMPARE(recovered.replayGainMode(), QStringLiteral("off"));
}

void SettingsPrefsTest::mutePersists()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini = dir.filePath(QStringLiteral("test.ini"));

    {
        Settings settings(ini);
        QVERIFY(!settings.muted());
        QSignalSpy spy(&settings, &Settings::mutedChanged);
        settings.setMuted(true);
        settings.setMuted(true);
        QCOMPARE(spy.count(), 1);
    }

    Settings reloaded(ini);
    QVERIFY(reloaded.muted());
}

void SettingsPrefsTest::inputMapViewSurvivesSettingsWrites()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini = dir.filePath(QStringLiteral("test.ini"));

    // InputMap deliberately opens its own QSettings on the same path (it owns
    // the whole [input] group). Qt shares one QConfFile per path within a
    // process, so the two views stay consistent — this is the regression test
    // for anything in Settings that starts writing near that group.
    InputMap input(ini);
    Settings settings(ini);

    const QString action = input.actionIds().value(0);
    QVERIFY(!action.isEmpty());
    QVERIFY(input.setBinding(action, QStringLiteral("Ctrl+Shift+F9")));

    settings.setDensityMode(QStringLiteral("compact"));
    settings.setThemeAccent(QStringLiteral("emby"));
    settings.setVolume(42);
    settings.setMuted(true);

    QCOMPARE(input.binding(action), QStringLiteral("Ctrl+Shift+F9"));
    QVERIFY(input.isCustomised(action));

    InputMap reloadedInput(ini);
    Settings reloadedSettings(ini);
    QCOMPARE(reloadedInput.binding(action), QStringLiteral("Ctrl+Shift+F9"));
    QCOMPARE(reloadedSettings.densityMode(), QStringLiteral("compact"));
    QCOMPARE(reloadedSettings.themeAccent(), QStringLiteral("emby"));
    QCOMPARE(reloadedSettings.volume(), 42);
    QVERIFY(reloadedSettings.muted());
}

// Auto-play is on unless the user turns it off: that is the behaviour of every
// TV app and of Emby's own web player, so an install that has never been
// configured must already do it.
void SettingsPrefsTest::autoPlayNextEpisodeDefaultsOn()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini = dir.filePath(QStringLiteral("autoplay.ini"));
    {
        Settings fresh(ini);
        QVERIFY2(fresh.autoPlayNextEpisode(), "a fresh install must auto-play");
        fresh.setAutoPlayNextEpisode(false);
        QVERIFY(!fresh.autoPlayNextEpisode());
    }
    // And the choice survives a restart, or turning it off is meaningless.
    Settings reloaded(ini);
    QVERIFY(!reloaded.autoPlayNextEpisode());
}

QTEST_GUILESS_MAIN(SettingsPrefsTest)
#include "tst_settings_prefs.moc"
