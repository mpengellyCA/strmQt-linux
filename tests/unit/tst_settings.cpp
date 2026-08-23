#include <QtTest>

#include "core/Settings.h"

using strmqt::Settings;

class SettingsTest : public QObject
{
    Q_OBJECT

private slots:
    void defaultServerUrl();
    void serverUrlRoundTrip();
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

QTEST_GUILESS_MAIN(SettingsTest)
#include "tst_settings.moc"
