#include <QSettings>
#include <QtTest>

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

QTEST_GUILESS_MAIN(SettingsTest)
#include "tst_settings.moc"
