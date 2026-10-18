#include "app/FirefoxCookies.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>

#include <algorithm>
#include <optional>
#include <sqlite3.h>

namespace strmqt::firefox {

namespace {

// Firefox keeps the store open, and in write-ahead mode: the file by itself
// can be minutes behind. A copy of the file and its log, opened as its own
// database, is the store as it stands and cannot disturb the original.
class Snapshot
{
public:
    explicit Snapshot(const QString &database)
    {
        if (!m_dir.isValid())
            return;
        const QString copy = m_dir.filePath(QStringLiteral("cookies.sqlite"));
        if (!QFile::copy(database, copy))
            return;
        QFile::copy(database + QStringLiteral("-wal"), copy + QStringLiteral("-wal"));
        if (sqlite3_open_v2(QFile::encodeName(copy).constData(), &m_db,
                            SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
    }
    ~Snapshot() { sqlite3_close(m_db); }
    Snapshot(const Snapshot &) = delete;
    Snapshot &operator=(const Snapshot &) = delete;

    sqlite3 *db() const { return m_db; }

private:
    QTemporaryDir m_dir;
    sqlite3 *m_db = nullptr;
};

QString text(sqlite3_stmt *statement, int column)
{
    const auto *bytes = sqlite3_column_text(statement, column);
    return bytes ? QString::fromUtf8(reinterpret_cast<const char *>(bytes),
                                     sqlite3_column_bytes(statement, column))
                 : QString();
}

// "^userContextId=2&…" → 2; 0 for the ordinary tabs.
int containerId(const QString &originAttributes)
{
    const QStringList parts = QString(originAttributes).remove(QLatin1Char('^'))
                                  .split(QLatin1Char('&'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part.startsWith(QLatin1String("userContextId=")))
            return part.mid(14).toInt();
    }
    return 0;
}

QString containerName(const QString &profileDir, int id)
{
    QFile file(QDir(profileDir).filePath(QStringLiteral("containers.json")));
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonArray identities = QJsonDocument::fromJson(file.readAll())
                                          .object().value(QLatin1String("identities")).toArray();
        for (const QJsonValue &value : identities) {
            const QJsonObject identity = value.toObject();
            if (identity.value(QLatin1String("userContextId")).toInt() != id)
                continue;
            const QString name = identity.value(QLatin1String("name")).toString();
            if (!name.isEmpty())
                return name;
            // The four Firefox ships with are named by a translation key.
            const QString key = identity.value(QLatin1String("l10nId")).toString(
                identity.value(QLatin1String("l10nID")).toString());
            if (key.contains(QLatin1String("personal"), Qt::CaseInsensitive))
                return QStringLiteral("Personal");
            if (key.contains(QLatin1String("work"), Qt::CaseInsensitive))
                return QStringLiteral("Work");
            if (key.contains(QLatin1String("banking"), Qt::CaseInsensitive))
                return QStringLiteral("Banking");
            if (key.contains(QLatin1String("shopping"), Qt::CaseInsensitive))
                return QStringLiteral("Shopping");
        }
    }
    return QStringLiteral("Container %1").arg(id);
}

// Firefox wrote seconds until it wrote milliseconds (and microseconds for
// lastAccessed all along); the magnitude says which.
qint64 toSeconds(qint64 stamp)
{
    while (stamp > 100000000000LL)
        stamp /= 1000;
    return stamp;
}

bool wanted(const QString &host)
{
    const QString bare = host.startsWith(QLatin1Char('.')) ? host.mid(1) : host;
    for (const char *domain : { "youtube.com", "google.com" }) {
        const QLatin1String d(domain);
        if (bare == d || bare.endsWith(QLatin1Char('.') + d))
            return true;
    }
    return false;
}

// What moz_cookies holds of a cookie, from either a store or a JSON row.
struct Row
{
    QString name;
    QString value;
    QString host;
    QString path;
    qint64 expiry = 0;
    bool secure = false;
    bool httpOnly = false;
    int sameSite = 0;
};

// The cookie `row` is, or none: not YouTube's or Google's, or expired.
std::optional<QNetworkCookie> toCookie(const Row &row, qint64 now)
{
    if (!wanted(row.host))
        return std::nullopt;
    const qint64 expiry = toSeconds(row.expiry);
    if (expiry > 0 && expiry <= now)
        return std::nullopt;
    QNetworkCookie cookie(row.name.toUtf8(), row.value.toUtf8());
    cookie.setDomain(row.host);
    cookie.setPath(row.path);
    if (expiry > 0)
        cookie.setExpirationDate(QDateTime::fromSecsSinceEpoch(expiry));
    cookie.setSecure(row.secure);
    cookie.setHttpOnly(row.httpOnly);
    // Firefox: 0 none, 1 lax, 2 strict (and 256 "unset" in newer ones).
    // "None" on a cookie that is not Secure is how older Firefox wrote
    // "nothing was said", and Chromium refuses that pair outright: such
    // a cookie is left to the engine's default.
    switch (row.sameSite) {
    case 0:
        if (cookie.isSecure())
            cookie.setSameSitePolicy(QNetworkCookie::SameSite::None);
        break;
    case 1: cookie.setSameSitePolicy(QNetworkCookie::SameSite::Lax); break;
    case 2: cookie.setSameSitePolicy(QNetworkCookie::SameSite::Strict); break;
    default: break;
    }
    return cookie;
}

// The cookies sources() takes for a sign-in.
bool isSignIn(const QNetworkCookie &cookie)
{
    return (cookie.name() == "SID" || cookie.name() == "__Secure-3PSID")
           && (cookie.domain() == QLatin1String(".youtube.com")
               || cookie.domain() == QLatin1String(".google.com"));
}

} // namespace

QString Source::label() const
{
    return containerName.isEmpty()
        ? QStringLiteral("Firefox · %1").arg(profileName)
        : QStringLiteral("Firefox · %1 · %2").arg(profileName, containerName);
}

QStringList defaultRoots()
{
    const QString home = QDir::homePath();
    return { home + QStringLiteral("/.mozilla/firefox"),
             home + QStringLiteral("/.config/mozilla/firefox"),
             home + QStringLiteral("/snap/firefox/common/.mozilla/firefox"),
             home + QStringLiteral("/.var/app/org.mozilla.firefox/.mozilla/firefox") };
}

QStringList profileDirectories(const QStringList &roots)
{
    QStringList first, rest;
    for (const QString &root : roots) {
        const QString ini = QDir(root).filePath(QStringLiteral("profiles.ini"));
        if (!QFileInfo::exists(ini))
            continue;
        const QSettings settings(ini, QSettings::IniFormat);
        QStringList installDefaults;
        const QStringList groups = settings.childGroups();
        for (const QString &group : groups) {
            if (group.startsWith(QLatin1String("Install")))
                installDefaults << settings.value(group + QStringLiteral("/Default")).toString();
        }
        for (const QString &group : groups) {
            if (!group.startsWith(QLatin1String("Profile")))
                continue;
            const QString path = settings.value(group + QStringLiteral("/Path")).toString();
            if (path.isEmpty())
                continue;
            const bool relative = settings.value(group + QStringLiteral("/IsRelative"), 1).toInt() != 0;
            const QString dir = QDir::cleanPath(relative ? QDir(root).filePath(path) : path);
            if (!QFileInfo::exists(QDir(dir).filePath(QStringLiteral("cookies.sqlite"))))
                continue;
            if (first.contains(dir) || rest.contains(dir))
                continue;
            (installDefaults.contains(path) ? first : rest) << dir;
        }
    }
    return first + rest;
}

QList<Source> sources(const QStringList &profileDirs)
{
    QList<Source> found;
    for (const QString &dir : profileDirs) {
        const QString database = QDir(dir).filePath(QStringLiteral("cookies.sqlite"));
        const Snapshot snapshot(database);
        if (!snapshot.db())
            continue;
        // A profile directory is "<salt>.<name>".
        const QString folder = QFileInfo(dir).fileName();
        const QString profileName = folder.contains(QLatin1Char('.'))
            ? folder.section(QLatin1Char('.'), 1) : folder;

        sqlite3_stmt *statement = nullptr;
        const char *query =
            "SELECT originAttributes,"
            " MAX(CASE WHEN name IN ('SID', '__Secure-3PSID')"
            "           AND (host = '.youtube.com' OR host = '.google.com') THEN 1 ELSE 0 END),"
            " MAX(lastAccessed)"
            " FROM moz_cookies"
            " WHERE host = 'youtube.com' OR host LIKE '%.youtube.com'"
            "    OR host = 'google.com' OR host LIKE '%.google.com'"
            " GROUP BY originAttributes";
        if (sqlite3_prepare_v2(snapshot.db(), query, -1, &statement, nullptr) != SQLITE_OK)
            continue;
        while (sqlite3_step(statement) == SQLITE_ROW) {
            Source source;
            source.originAttributes = text(statement, 0);
            // A cookie kept apart for one site's embedded frames, or for a
            // private window, is not a sign-in anyone chose.
            if (source.originAttributes.contains(QLatin1String("partitionKey"))
                || source.originAttributes.contains(QLatin1String("privateBrowsingId")))
                continue;
            source.profileName = profileName;
            source.database = database;
            source.signedIn = sqlite3_column_int(statement, 1) != 0;
            source.lastUsed = toSeconds(sqlite3_column_int64(statement, 2));
            const int container = containerId(source.originAttributes);
            if (container > 0)
                source.containerName = containerName(dir, container);
            found << source;
        }
        sqlite3_finalize(statement);
    }
    std::stable_sort(found.begin(), found.end(), [](const Source &a, const Source &b) {
        return a.signedIn && !b.signedIn;
    });
    return found;
}

QList<QNetworkCookie> cookies(const Source &source, QString *error)
{
    QList<QNetworkCookie> result;
    const auto fail = [error](const QString &why) {
        if (error)
            *error = why;
    };
    const Snapshot snapshot(source.database);
    if (!snapshot.db()) {
        fail(QStringLiteral("Firefox's cookie store could not be opened."));
        return result;
    }
    sqlite3_stmt *statement = nullptr;
    const char *query = "SELECT name, value, host, path, expiry, isSecure, isHttpOnly, sameSite"
                        " FROM moz_cookies WHERE originAttributes = ?1";
    if (sqlite3_prepare_v2(snapshot.db(), query, -1, &statement, nullptr) != SQLITE_OK) {
        fail(QStringLiteral("Firefox's cookie store is not in a form this version understands."));
        return result;
    }
    const QByteArray attributes = source.originAttributes.toUtf8();
    sqlite3_bind_text(statement, 1, attributes.constData(), int(attributes.size()), SQLITE_STATIC);
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const Row row{text(statement, 0),
                      text(statement, 1),
                      text(statement, 2),
                      text(statement, 3),
                      sqlite3_column_int64(statement, 4),
                      sqlite3_column_int(statement, 5) != 0,
                      sqlite3_column_int(statement, 6) != 0,
                      sqlite3_column_int(statement, 7)};
        if (const auto cookie = toCookie(row, now))
            result << *cookie;
    }
    sqlite3_finalize(statement);
    if (result.isEmpty())
        fail(QStringLiteral("There is no YouTube sign-in there."));
    return result;
}

Carried carried(const QByteArray &json, QString *error)
{
    Carried result;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (!document.isObject()) {
        if (error)
            *error = QStringLiteral("That is not a sign-in StrmQt can read.");
        return result;
    }
    const QJsonObject root = document.object();
    result.label = root.value(QLatin1String("label")).toString().simplified().left(120);
    if (result.label.isEmpty())
        result.label = QStringLiteral("Firefox on another computer");
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QJsonArray rows = root.value(QLatin1String("cookies")).toArray();
    for (const QJsonValue &value : rows) {
        const QJsonObject object = value.toObject();
        const Row row{object.value(QLatin1String("name")).toString(),
                      object.value(QLatin1String("value")).toString(),
                      object.value(QLatin1String("host")).toString(),
                      object.value(QLatin1String("path")).toString(QStringLiteral("/")),
                      qint64(object.value(QLatin1String("expiry")).toDouble()),
                      object.value(QLatin1String("isSecure")).toInt() != 0,
                      object.value(QLatin1String("isHttpOnly")).toInt() != 0,
                      object.value(QLatin1String("sameSite")).toInt()};
        if (row.name.isEmpty())
            continue;
        if (const auto cookie = toCookie(row, now)) {
            result.signedIn = result.signedIn || isSignIn(*cookie);
            result.cookies << *cookie;
        }
    }
    if (result.cookies.isEmpty() && error)
        *error = QStringLiteral("There is no YouTube sign-in there.");
    return result;
}

} // namespace strmqt::firefox
