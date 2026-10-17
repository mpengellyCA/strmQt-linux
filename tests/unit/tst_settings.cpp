#include <QFile>
#include <QDir>
#include <QSettings>
#include <QSignalSpy>
#include <QtTest>

#include <algorithm>

#include "core/Settings.h"

using strmqt::Settings;

class SettingsTest : public QObject
{
    Q_OBJECT

private slots:
    void defaultServerUrl();
    void serverUrlRoundTrip();
    void retainedPlaybackStateIsSessionScoped();
    void preScopingKeysAreAdoptedByTheFirstSessionOnly();
    void crashResumeWritesAreDebouncedUntilFlush();
    void retainedPlaybackChoicesAreBoundedAndKeepRecentEntries();
    void oversizedLegacyChoicesArePrunedWhenSessionRestores();
    void accountProfileRemovalClearsSession();
    void webRemoteIsOffAndPinnedByDefault();
    void webRemotePinExistsFromConstruction();
    void webRemotePinIsKeptAcrossRuns();
    void theAppsOwnSettingsMakeAPinToo();
    void unusableWebRemotePinIsReplaced_data();
    void unusableWebRemotePinIsReplaced();
    void settingAnUnusableWebRemotePinStoresAFreshOne();
    void youtubeAndYoutubeMusicAreSwitchedSeparately();
};

void SettingsTest::defaultServerUrl()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Settings settings(dir.filePath(QStringLiteral("test.ini")));
    // No baked-in server. The artifacts are distributable, so a default host
    // would be both a privacy leak and wrong for every user but one; the login
    // screen asks instead. This assertion is the guard against it coming back.
    QVERIFY2(settings.serverUrl().isEmpty(),
             qPrintable(settings.serverUrl().toString()));
}

void SettingsTest::serverUrlRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini = dir.filePath(QStringLiteral("test.ini"));

    {
        Settings settings(ini);
        QSignalSpy spy(&settings, &Settings::serverUrlChanged);
        settings.setServerUrl(QUrl(QStringLiteral("https://example.org:8920")));
        QCOMPARE(spy.count(), 1);
        // Setting the same value again must not re-emit.
        settings.setServerUrl(QUrl(QStringLiteral("https://example.org:8920")));
        QCOMPARE(spy.count(), 1);
    }

    Settings reloaded(ini);
    QCOMPARE(reloaded.serverUrl(), QUrl(QStringLiteral("https://example.org:8920")));
}

void SettingsTest::retainedPlaybackStateIsSessionScoped()
{
    QTemporaryDir dir;
    Settings settings(dir.filePath(QStringLiteral("scoped.ini")));
    settings.setServerUrl(QUrl(QStringLiteral("https://one.example")));
    settings.setUserId(QStringLiteral("alice"));
    settings.setLibraryViewMode(QStringLiteral("movies"), QStringLiteral("list"));
    settings.rememberTracks(QStringLiteral("item"), QStringLiteral("source"), 2, -1);
    settings.rememberVersion(QStringLiteral("item"), QStringLiteral("source"));
    settings.setLastPlayback(QStringLiteral("item"), QStringLiteral("Film"), 42'000);

    settings.setUserId(QStringLiteral("bob"));
    QVERIFY(settings.libraryViewMode(QStringLiteral("movies")).isEmpty());
    QVERIFY(!settings.hasRememberedTracks(QStringLiteral("item"), QStringLiteral("source")));
    QVERIFY(settings.rememberedVersion(QStringLiteral("item")).isEmpty());
    QVERIFY(settings.lastPlayback().isEmpty());

    settings.setUserId(QStringLiteral("alice"));
    QCOMPARE(settings.libraryViewMode(QStringLiteral("movies")), QStringLiteral("list"));
    QCOMPARE(settings.recalledTracks(QStringLiteral("item"), QStringLiteral("source")),
             (QPair<int, int>(2, -1)));
    QCOMPARE(settings.rememberedVersion(QStringLiteral("item")), QStringLiteral("source"));
    QCOMPARE(settings.lastPlayback().value(QStringLiteral("positionMs")).toLongLong(),
             Q_INT64_C(42'000));

    settings.setServerUrl(QUrl(QStringLiteral("https://two.example")));
    QVERIFY(settings.lastPlayback().isEmpty());
}

// Session scoping arrived after these keys had been written flat, so an upgrade
// would otherwise read as "the app forgot everything". The first session to run
// the migration adopts them; a second account must not inherit them.
void SettingsTest::preScopingKeysAreAdoptedByTheFirstSessionOnly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("test.ini"));

    {
        QSettings legacy(path, QSettings::IniFormat);
        legacy.setValue(QStringLiteral("resume/itemId"), QStringLiteral("301001"));
        legacy.setValue(QStringLiteral("resume/title"), QStringLiteral("The Matrix"));
        legacy.setValue(QStringLiteral("resume/positionMs"), 42'000);
        legacy.setValue(QStringLiteral("libraryView/movies/mode"), QStringLiteral("grid"));
        legacy.setValue(QStringLiteral("versions/301001"), QStringLiteral("source-a"));
        legacy.sync();
    }

    Settings settings(path);
    settings.setServerUrl(QUrl(QStringLiteral("https://one.example")));
    settings.setUserId(QStringLiteral("user-one"));
    settings.migrateLegacySessionData();

    QCOMPARE(settings.lastPlayback().value(QStringLiteral("itemId")).toString(),
             QStringLiteral("301001"));
    QCOMPARE(settings.lastPlayback().value(QStringLiteral("positionMs")).toLongLong(),
             Q_INT64_C(42'000));
    QCOMPARE(settings.libraryViewMode(QStringLiteral("movies")), QStringLiteral("grid"));
    QCOMPARE(settings.rememberedVersion(QStringLiteral("301001")), QStringLiteral("source-a"));

    // The flat keys are gone, so nothing can be adopted twice.
    {
        QSettings raw(path, QSettings::IniFormat);
        QVERIFY(!raw.contains(QStringLiteral("resume/itemId")));
        QVERIFY(!raw.contains(QStringLiteral("libraryView/movies/mode")));
    }

    settings.setUserId(QStringLiteral("user-two"));
    settings.migrateLegacySessionData();
    QVERIFY(settings.lastPlayback().isEmpty());
    QVERIFY(settings.libraryViewMode(QStringLiteral("movies")).isEmpty());
}

void SettingsTest::crashResumeWritesAreDebouncedUntilFlush()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("resume.ini"));
    Settings settings(path);
    settings.setServerUrl(QUrl(QStringLiteral("https://one.example")));
    settings.setUserId(QStringLiteral("alice"));

    settings.setLastPlayback(QStringLiteral("item"), QStringLiteral("Film"), 1000);
    const auto durableContents = [&path] {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return QByteArray();
        return file.readAll();
    };
    QVERIFY(durableContents().contains("positionMs=1000"));

    settings.setLastPlayback(QStringLiteral("item"), QStringLiteral("Film"), 2000);
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    QVERIFY(durableContents().contains("positionMs=1000"));
    QVERIFY(!durableContents().contains("positionMs=2000"));

    settings.flush();
    QVERIFY(durableContents().contains("positionMs=2000"));

    // A different item gets its own immediate first checkpoint. A clean stop
    // resets that identity boundary for the item that follows it as well.
    settings.setLastPlayback(QStringLiteral("item-two"), QStringLiteral("Second"), 3000);
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    QVERIFY(durableContents().contains("itemId=item-two"));
    QVERIFY(durableContents().contains("positionMs=3000"));

    settings.clearLastPlayback();
    settings.setLastPlayback(QStringLiteral("item-three"), QStringLiteral("Third"), 4000);
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    QVERIFY(durableContents().contains("itemId=item-three"));
    QVERIFY(durableContents().contains("positionMs=4000"));
}

void SettingsTest::retainedPlaybackChoicesAreBoundedAndKeepRecentEntries()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("retained.ini"));
    Settings settings(path);
    settings.setServerUrl(QUrl(QStringLiteral("https://one.example")));
    settings.setUserId(QStringLiteral("alice"));

    for (int i = 0; i < 300; ++i) {
        const QString item = QStringLiteral("item-%1").arg(i, 3, 10, QLatin1Char('0'));
        settings.rememberTracks(item, QStringLiteral("source"), i, -1);
        settings.rememberVersion(item, QStringLiteral("source"));
    }
    settings.flush();

    QSettings raw(path, QSettings::IniFormat);
    const QStringList keys = raw.allKeys();
    QCOMPARE(keys.filter(QStringLiteral("/tracks/")).size(), 256);
    QCOMPARE(keys.filter(QStringLiteral("/versions/")).size(), 256);
    QVERIFY(!settings.hasRememberedTracks(QStringLiteral("item-000"),
                                          QStringLiteral("source")));
    QVERIFY(settings.hasRememberedTracks(QStringLiteral("item-299"),
                                         QStringLiteral("source")));
    QVERIFY(settings.rememberedVersion(QStringLiteral("item-000")).isEmpty());
    QCOMPARE(settings.rememberedVersion(QStringLiteral("item-299")),
             QStringLiteral("source"));
}

void SettingsTest::oversizedLegacyChoicesArePrunedWhenSessionRestores()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("legacy-retained.ini"));
    {
        Settings seed(path);
        seed.setServerUrl(QUrl(QStringLiteral("https://one.example")));
        seed.setUserId(QStringLiteral("alice"));
        seed.rememberVersion(QStringLiteral("seed"), QStringLiteral("source"));
        seed.flush();
    }

    QSettings raw(path, QSettings::IniFormat);
    const QString seedKey = raw.allKeys().filter(QStringLiteral("/versions/seed")).value(0);
    QVERIFY(!seedKey.isEmpty());
    const QString prefix = seedKey.left(seedKey.indexOf(QStringLiteral("versions/")));
    raw.remove(prefix + QStringLiteral("retainedOrder/versions"));
    raw.setValue(QStringLiteral("migration/sessionScopeAdopted"), true);
    for (int i = 0; i < 300; ++i)
        raw.setValue(prefix + QStringLiteral("versions/legacy-%1").arg(i),
                     QStringLiteral("source"));
    raw.sync();

    Settings restored(path);
    restored.migrateLegacySessionData();
    restored.flush();

    QSettings pruned(path, QSettings::IniFormat);
    QCOMPARE(pruned.allKeys().filter(QStringLiteral("/versions/")).size(), 256);
}

void SettingsTest::accountProfileRemovalClearsSession()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("remove.ini"));
    Settings settings(path);
    settings.upsertAccountProfile(QUrl(QStringLiteral("https://one.example")), QStringLiteral("alice"), QStringLiteral("Alice"));
    
    settings.setServerUrl(QUrl(QStringLiteral("https://one.example")));
    settings.setUserId(QStringLiteral("alice"));
    settings.setLibraryViewMode(QStringLiteral("movies"), QStringLiteral("list"));
    settings.flush();

    settings.removeAccountProfile(QUrl(QStringLiteral("https://one.example")), QStringLiteral("alice"));

    QSettings raw(path, QSettings::IniFormat);
    const QString scope = Settings::sessionScopeFor(QUrl(QStringLiteral("https://one.example")), QStringLiteral("alice"));
    const QString sessionKey = QStringLiteral("sessions/%1/libraryView/movies/mode").arg(scope);
    QVERIFY(!raw.contains(sessionKey));
}

void SettingsTest::webRemoteIsOffAndPinnedByDefault()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini = dir.filePath(QStringLiteral("test.ini"));
    {
        Settings settings(ini);
        // The phone remote is a network listener that can drive playback: it
        // is opt-in, and once on, a phone has to know the PIN.
        QVERIFY(!settings.webRemoteEnabled());
        QVERIFY(settings.webRemoteRequirePin());
        QVERIFY(!settings.webRemoteSetupOffered());
        settings.setWebRemoteSetupOffered(true);
    }
    Settings settings(ini);
    QVERIFY(settings.webRemoteSetupOffered());
}

namespace {
bool isFourDigits(const QString &pin)
{
    return pin.size() == 4 && std::all_of(pin.cbegin(), pin.cend(), [](QChar c) {
        return c >= QLatin1Char('0') && c <= QLatin1Char('9');
    });
}
} // namespace

// The PIN is made when Settings is, not on first read: webRemotePin() is a
// const getter and must not be the thing that writes the store.
void SettingsTest::webRemotePinExistsFromConstruction()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini = dir.filePath(QStringLiteral("test.ini"));

    Settings settings(ini);
    QSignalSpy spy(&settings, &Settings::webRemotePinChanged);
    settings.flush();
    // Nothing has asked for the PIN yet, and it is already on disk.
    const QString stored =
        QSettings(ini, QSettings::IniFormat).value(QStringLiteral("remote/pin")).toString();
    QVERIFY2(isFourDigits(stored), qPrintable(stored));

    const Settings &readOnly = settings;
    QCOMPARE(readOnly.webRemotePin(), stored);
    QCOMPARE(readOnly.webRemotePin(), stored);
    // Reading is not changing: no phone is signed out by a look at the PIN.
    QCOMPARE(spy.count(), 0);
}

// The app constructs Settings with no path (QSettings' own location), the
// tests above with one. Both constructors have to make the PIN: one missing
// from the app's would hand the web remote an empty PIN to compare against.
void SettingsTest::theAppsOwnSettingsMakeAPinToo()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // QSettings' user scope, moved under the temporary directory for this test
    // so the real settings file is never read or written.
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, dir.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());

    QString generated;
    {
        Settings settings;
        generated = settings.webRemotePin();
        QVERIFY2(isFourDigits(generated), qPrintable(generated));
        settings.flush();
    }
    QVERIFY(QDir(dir.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size() > 0);
    Settings again;
    QCOMPARE(again.webRemotePin(), generated);
}

void SettingsTest::webRemotePinIsKeptAcrossRuns()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini = dir.filePath(QStringLiteral("test.ini"));

    QString generated;
    {
        Settings settings(ini);
        generated = settings.webRemotePin();
    }
    {
        // The PIN a phone was given survives a restart…
        Settings settings(ini);
        QCOMPARE(settings.webRemotePin(), generated);
    }
    {
        // …including one that happens to start with zeros, and one the user chose.
        QSettings raw(ini, QSettings::IniFormat);
        raw.setValue(QStringLiteral("remote/pin"), QStringLiteral("0007"));
    }
    {
        Settings settings(ini);
        QCOMPARE(settings.webRemotePin(), QStringLiteral("0007"));
        QSignalSpy spy(&settings, &Settings::webRemotePinChanged);
        settings.setWebRemotePin(QStringLiteral("0007"));
        QCOMPARE(spy.count(), 0);
        settings.setWebRemotePin(QStringLiteral("4321"));
        QCOMPARE(spy.count(), 1);
    }
    Settings settings(ini);
    QCOMPARE(settings.webRemotePin(), QStringLiteral("4321"));
}

void SettingsTest::unusableWebRemotePinIsReplaced_data()
{
    QTest::addColumn<QString>("stored");
    QTest::newRow("empty") << QString();
    QTest::newRow("too-short") << QStringLiteral("12");
    QTest::newRow("too-long") << QStringLiteral("123456");
    QTest::newRow("letters") << QStringLiteral("abcd");
    QTest::newRow("signed") << QStringLiteral("-123");
    QTest::newRow("padded") << QStringLiteral(" 123");
}

// A hand-edited or damaged PIN is never served: a short or guessable one would
// be the only thing between the LAN and the remote.
void SettingsTest::unusableWebRemotePinIsReplaced()
{
    QFETCH(QString, stored);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini = dir.filePath(QStringLiteral("test.ini"));
    {
        QSettings raw(ini, QSettings::IniFormat);
        raw.setValue(QStringLiteral("remote/pin"), stored);
    }
    QString replaced;
    {
        Settings settings(ini);
        replaced = settings.webRemotePin();
        QVERIFY2(isFourDigits(replaced), qPrintable(replaced));
        QVERIFY(replaced != stored);
    }
    Settings settings(ini);
    QCOMPARE(settings.webRemotePin(), replaced);
}

void SettingsTest::settingAnUnusableWebRemotePinStoresAFreshOne()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini = dir.filePath(QStringLiteral("test.ini"));
    QString fresh;
    {
        Settings settings(ini);
        settings.setWebRemotePin(QStringLiteral("2468"));
        QSignalSpy spy(&settings, &Settings::webRemotePinChanged);
        settings.setWebRemotePin(QStringLiteral("12"));
        fresh = settings.webRemotePin();
        QVERIFY2(isFourDigits(fresh), qPrintable(fresh));
        // Whatever replaced it, the old PIN is gone and its phones with it —
        // unless the dice landed on the same four digits.
        QCOMPARE(spy.count(), fresh == QStringLiteral("2468") ? 0 : 1);
        QCOMPARE(settings.webRemotePin(), fresh);
    }
    Settings settings(ini);
    QCOMPARE(settings.webRemotePin(), fresh);
}

void SettingsTest::youtubeAndYoutubeMusicAreSwitchedSeparately()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini = dir.filePath(QStringLiteral("test.ini"));
    {
        Settings settings(ini);
        // Both there unless the user says otherwise.
        QVERIFY(settings.youtubeEnabled());
        QVERIFY(settings.youtubeMusicEnabled());

        QSignalSpy youtube(&settings, &Settings::youtubeEnabledChanged);
        QSignalSpy music(&settings, &Settings::youtubeMusicEnabledChanged);
        settings.setYoutubeMusicEnabled(false);
        settings.setYoutubeMusicEnabled(false);
        QCOMPARE(music.count(), 1);
        QCOMPARE(youtube.count(), 0);
        QVERIFY(settings.youtubeEnabled());
        QVERIFY(!settings.youtubeMusicEnabled());
    }
    {
        Settings settings(ini);
        QVERIFY(settings.youtubeEnabled());
        QVERIFY(!settings.youtubeMusicEnabled());
        settings.setYoutubeEnabled(false);
        settings.setYoutubeMusicEnabled(true);
    }
    Settings settings(ini);
    QVERIFY(!settings.youtubeEnabled());
    QVERIFY(settings.youtubeMusicEnabled());
}

QTEST_GUILESS_MAIN(SettingsTest)
#include "tst_settings.moc"
