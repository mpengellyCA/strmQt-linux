// The Firefox side of signing in to YouTube Music (app/FirefoxCookies.h),
// against cookie stores built here: no real profile is read.
#include "app/FirefoxCookies.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <sqlite3.h>

using namespace strmqt::firefox;

namespace {

struct Row
{
    const char *name;
    const char *value;
    const char *host;
    qint64 expiry;
    const char *originAttributes = "";
    int sameSite = 0;
};

void writeStore(const QString &path, const QList<Row> &rows)
{
    sqlite3 *db = nullptr;
    QCOMPARE(sqlite3_open(QFile::encodeName(path).constData(), &db), SQLITE_OK);
    QCOMPARE(sqlite3_exec(db,
                          "CREATE TABLE moz_cookies (id INTEGER PRIMARY KEY, originAttributes TEXT"
                          " NOT NULL DEFAULT '', name TEXT, value TEXT, host TEXT, path TEXT,"
                          " expiry INTEGER, lastAccessed INTEGER, creationTime INTEGER,"
                          " isSecure INTEGER, isHttpOnly INTEGER, sameSite INTEGER)",
                          nullptr, nullptr, nullptr),
             SQLITE_OK);
    for (const Row &row : rows) {
        sqlite3_stmt *statement = nullptr;
        QCOMPARE(sqlite3_prepare_v2(db,
                                    "INSERT INTO moz_cookies (originAttributes, name, value, host,"
                                    " path, expiry, lastAccessed, isSecure, isHttpOnly, sameSite)"
                                    " VALUES (?1, ?2, ?3, ?4, '/', ?5, 1790000000000000, 1, 1, ?6)",
                                    -1, &statement, nullptr),
                 SQLITE_OK);
        sqlite3_bind_text(statement, 1, row.originAttributes, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 2, row.name, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 3, row.value, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 4, row.host, -1, SQLITE_STATIC);
        sqlite3_bind_int64(statement, 5, row.expiry);
        sqlite3_bind_int(statement, 6, row.sameSite);
        QCOMPARE(sqlite3_step(statement), SQLITE_DONE);
        sqlite3_finalize(statement);
    }
    sqlite3_close(db);
}

void writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

} // namespace

class tst_FirefoxCookies : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_dir.reset(new QTemporaryDir);
        QVERIFY(m_dir->isValid());
        m_root = m_dir->path();
        m_soon = QDateTime::currentSecsSinceEpoch() + 86400;
    }

    // profiles.ini names them; the installation's default comes first, and a
    // profile with no cookie store is not one to offer.
    void profilesAreFoundDefaultFirst()
    {
        for (const char *name : { "aaaa.default", "bbbb.default-release", "cccc.empty" })
            QVERIFY(QDir(m_root).mkpath(QString::fromLatin1(name)));
        writeStore(m_root + "/aaaa.default/cookies.sqlite", {});
        writeStore(m_root + "/bbbb.default-release/cookies.sqlite", {});
        writeFile(m_root + "/profiles.ini",
                  "[InstallABC]\nDefault=bbbb.default-release\nLocked=1\n\n"
                  "[Profile2]\nName=empty\nIsRelative=1\nPath=cccc.empty\n\n"
                  "[Profile1]\nName=default\nIsRelative=1\nPath=aaaa.default\nDefault=1\n\n"
                  "[Profile0]\nName=default-release\nIsRelative=1\nPath=bbbb.default-release\n\n"
                  "[General]\nStartWithLastProfile=1\nVersion=2\n");

        const QStringList dirs = profileDirectories({ m_root, m_root + "/nowhere" });
        QCOMPARE(dirs, (QStringList { m_root + "/bbbb.default-release", m_root + "/aaaa.default" }));
    }

    // Ordinary tabs and each container are separate sign-ins; cookies kept
    // apart for embedded frames or private windows are nobody's.
    void eachContainerIsItsOwnSource()
    {
        const QString profile = m_root + "/bbbb.default-release";
        QVERIFY(QDir().mkpath(profile));
        writeStore(profile + "/cookies.sqlite", {
            { "PREF", "x", ".youtube.com", m_soon },
            { "SID", "work", ".youtube.com", m_soon, "^userContextId=2" },
            { "SID", "framed", ".youtube.com", m_soon, "^partitionKey=%28https%2Cexample.com%29" },
            { "SID", "elsewhere", ".example.com", m_soon, "^userContextId=3" },
        });
        writeFile(profile + "/containers.json",
                  R"({"identities":[{"userContextId":2,"l10nId":"user-context-work","public":true}]})");

        const QList<Source> found = sources({ profile });
        QCOMPARE(found.size(), 2);
        // The signed-in one first.
        QCOMPARE(found.at(0).label(), QStringLiteral("Firefox · default-release · Work"));
        QVERIFY(found.at(0).signedIn);
        QCOMPARE(found.at(0).lastUsed, qint64(1790000000));
        QCOMPARE(found.at(1).label(), QStringLiteral("Firefox · default-release"));
        QVERIFY(!found.at(1).signedIn);
    }

    // Only YouTube's and Google's, only this source's, only the live ones —
    // and with everything a browser needs to send them back.
    void cookiesAreTheSourcesOwn()
    {
        const QString profile = m_root + "/aaaa.default";
        QVERIFY(QDir().mkpath(profile));
        writeStore(profile + "/cookies.sqlite", {
            { "SID", "mine", ".youtube.com", m_soon },
            { "__Secure-3PSID", "mine3", ".google.com", m_soon * 1000, "", 0 }, // milliseconds
            { "LOGIN_INFO", "info", "music.youtube.com", m_soon, "", 2 },
            { "SID", "old", ".youtube.com", 1000000000 },                // long expired
            { "SID", "other", ".youtube.com", m_soon, "^userContextId=2" },
            { "session", "bank", ".notgoogle.com", m_soon },
            { "session", "fake", ".evilyoutube.com", m_soon },
        });
        const QList<Source> found = sources({ profile });
        QCOMPARE(found.size(), 2);
        const Source ordinary = found.at(0).originAttributes.isEmpty() ? found.at(0) : found.at(1);

        QString error;
        const QList<QNetworkCookie> jar = cookies(ordinary, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(jar.size(), 3);
        QHash<QByteArray, QNetworkCookie> byValue;
        for (const QNetworkCookie &cookie : jar)
            byValue.insert(cookie.value(), cookie);
        QVERIFY(byValue.contains("mine") && byValue.contains("mine3") && byValue.contains("info"));

        const QNetworkCookie google = byValue.value("mine3");
        QCOMPARE(google.domain(), QStringLiteral(".google.com"));
        QCOMPARE(google.path(), QStringLiteral("/"));
        QVERIFY(google.isSecure());
        QVERIFY(google.isHttpOnly());
        QCOMPARE(google.expirationDate().toSecsSinceEpoch(), m_soon);
        QCOMPARE(google.sameSitePolicy(), QNetworkCookie::SameSite::None);
        QCOMPARE(byValue.value("info").sameSitePolicy(), QNetworkCookie::SameSite::Strict);
    }

    // Firefox has the store open while it runs, with the newest cookies
    // still in its write-ahead log. The log is read; the store is not
    // touched.
    void aStoreInUseIsReadWithoutBeingChanged()
    {
        const QString profile = m_root + "/aaaa.default";
        QVERIFY(QDir().mkpath(profile));
        const QString store = profile + "/cookies.sqlite";
        writeStore(store, {});

        sqlite3 *firefox = nullptr;
        QCOMPARE(sqlite3_open(QFile::encodeName(store).constData(), &firefox), SQLITE_OK);
        QCOMPARE(sqlite3_exec(firefox, "PRAGMA journal_mode=WAL; PRAGMA wal_autocheckpoint=0;",
                              nullptr, nullptr, nullptr), SQLITE_OK);
        const QByteArray insert = "INSERT INTO moz_cookies (name, value, host, path, expiry, isSecure,"
                                  " isHttpOnly, sameSite) VALUES ('SID', 'fresh', '.youtube.com', '/', "
                                  + QByteArray::number(m_soon) + ", 1, 1, 0)";
        QCOMPARE(sqlite3_exec(firefox, insert.constData(), nullptr, nullptr, nullptr), SQLITE_OK);
        QVERIFY(QFile::exists(store + "-wal"));
        const QDateTime before = QFileInfo(store).lastModified();
        const qint64 sizeBefore = QFileInfo(store).size();

        const QList<Source> found = sources({ profile });
        QCOMPARE(found.size(), 1);
        QVERIFY(found.at(0).signedIn);
        const QList<QNetworkCookie> jar = cookies(found.at(0));
        QCOMPARE(jar.size(), 1);
        QCOMPARE(jar.at(0).value(), QByteArray("fresh"));

        QCOMPARE(QFileInfo(store).lastModified(), before);
        QCOMPARE(QFileInfo(store).size(), sizeBefore);
        sqlite3_close(firefox);
    }

    void aMissingOrForeignStoreIsAnErrorNotACrash()
    {
        Source nowhere;
        nowhere.database = m_root + "/none/cookies.sqlite";
        QString error;
        QVERIFY(cookies(nowhere, &error).isEmpty());
        QVERIFY(!error.isEmpty());

        writeFile(m_root + "/cookies.sqlite", "this is not a database, it only has the name");
        Source foreign;
        foreign.database = m_root + "/cookies.sqlite";
        error.clear();
        QVERIFY(cookies(foreign, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(sources({ m_root }).isEmpty());
    }

private:
    QScopedPointer<QTemporaryDir> m_dir;
    QString m_root;
    qint64 m_soon = 0;
};

QTEST_GUILESS_MAIN(tst_FirefoxCookies)
#include "tst_firefox_cookies.moc"
