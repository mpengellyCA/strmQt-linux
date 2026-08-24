#include "Settings.h"

#include "Log.h"

#include <QRegularExpression>
#include <QCryptographicHash>

#include <QUuid>

namespace strmqt {

namespace {
const auto kServerUrlKey = QStringLiteral("server/url");
const auto kUsernameKey = QStringLiteral("server/username");
const auto kUserIdKey = QStringLiteral("server/userId");
const auto kDeviceIdKey = QStringLiteral("device/id");
const auto kEngineKey = QStringLiteral("playback/engine");
const auto kToneMappingKey = QStringLiteral("playback/toneMapping");
const auto kDensityKey = QStringLiteral("appearance/density");
const auto kThemeAccentKey = QStringLiteral("appearance/accent");
const auto kReducedMotionKey = QStringLiteral("appearance/reducedMotion");
const auto kVolumeKey = QStringLiteral("playback/volume");
const auto kMutedKey = QStringLiteral("playback/muted");
const auto kReplayGainKey = QStringLiteral("playback/replayGain");
const auto kLiveEnabledKey = QStringLiteral("live/enabled");
const auto kPollIntervalKey = QStringLiteral("live/pollIntervalSeconds");
const auto kLastItemKey = QStringLiteral("resume/itemId");
const auto kLastTitleKey = QStringLiteral("resume/title");
const auto kLastPositionKey = QStringLiteral("resume/positionMs");
// Set once, the first time a session scope exists to adopt the pre-scoping
// keys. Without it the second account to sign in would inherit the first's
// resume point and view preferences.
const auto kScopeMigratedKey = QStringLiteral("migration/sessionScopeAdopted");
// ARCHITECTURE.md: comfortable on the desk, TV when a gamepad takes over (F3);
// projection-booth amber is the default identity (F4).
const auto kDefaultDensity = QStringLiteral("comfortable");
const auto kDefaultAccent = QStringLiteral("projection");
constexpr int kDefaultVolume = 100;
constexpr int kMaxVolume = 130; // PlayerBackend::setVolume contract

// ARCHITECTURE.md: 60 s by default. The floor keeps a hand-edited INI from turning
// the fallback into a request flood; the ceiling keeps "1 day" from reading as
// "live updates are on" when they effectively are not.
constexpr int kDefaultPollSeconds = 60;
constexpr int kMinPollSeconds = 15;
constexpr int kMaxPollSeconds = 3600;
} // namespace

Settings::Settings(QObject *parent) : QObject(parent) {}

Settings::Settings(const QString &iniFilePath, QObject *parent)
    : QObject(parent), m_store(iniFilePath, QSettings::IniFormat)
{
}

QString Settings::sessionScope() const
{
    const QUrl url = serverUrl();
    const QString user = userId();
    if (url.isEmpty() || user.isEmpty())
        return {};
    const QByteArray identity = url.adjusted(QUrl::StripTrailingSlash)
                                    .toString(QUrl::FullyEncoded)
                                    .toUtf8() +
                                '\0' + user.toUtf8();
    return QString::fromLatin1(
        QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
}

// Per-session scoping (server+user) arrived after these keys had been written
// flat for one implicit session. Hand that data to the first session that can
// own it, exactly once, so an upgrade does not read as "everything forgotten".
void Settings::migrateLegacySessionData()
{
    const QString scope = sessionScope();
    if (scope.isEmpty() || m_store.value(kScopeMigratedKey, false).toBool())
        return;
    m_store.setValue(kScopeMigratedKey, true);

    QStringList legacyKeys{kLastItemKey, kLastTitleKey, kLastPositionKey};
    for (const QString &group : {QStringLiteral("libraryView"), QStringLiteral("tracks"),
                                 QStringLiteral("versions")}) {
        m_store.beginGroup(group);
        const QStringList keys = m_store.allKeys();
        m_store.endGroup();
        for (const QString &key : keys)
            legacyKeys.append(group + QLatin1Char('/') + key);
    }

    int adopted = 0;
    for (const QString &legacyKey : std::as_const(legacyKeys)) {
        if (!m_store.contains(legacyKey))
            continue;
        const QString scoped = QStringLiteral("sessions/%1/%2").arg(scope, legacyKey);
        if (!m_store.contains(scoped)) {
            m_store.setValue(scoped, m_store.value(legacyKey));
            ++adopted;
        }
        m_store.remove(legacyKey);
    }
    if (adopted > 0)
        qCInfo(logCore) << "adopted" << adopted << "pre-scoping settings into the current session";
    m_store.sync();
}

QString Settings::scopedKey(const QString &key) const
{
    const QString scope = sessionScope();
    return scope.isEmpty() ? QString() : QStringLiteral("sessions/%1/%2").arg(scope, key);
}

QUrl Settings::serverUrl() const
{
    // No default. A shipped build must not carry anyone's server address: the
    // artifacts are distributable, and a baked-in host is both a privacy leak
    // and a wrong answer for every user but one. An empty URL is the honest
    // starting state — the login screen asks for it.
    return m_store.value(kServerUrlKey).toUrl();
}

void Settings::setServerUrl(const QUrl &url)
{
    // The `contains` half is load-bearing, and its absence cost a release. When
    // serverUrl() still carried a baked-in default, signing in with the
    // pre-filled field meant url == serverUrl() and the value was never written
    // to the store at all — it only ever *looked* stored, because the default
    // answered every read. Removing the default then left those installs with
    // no address and a session that restored into a server-less app.
    if (url == serverUrl() && m_store.contains(kServerUrlKey))
        return;
    m_store.setValue(kServerUrlKey, url);
    emit serverUrlChanged();
}

QString Settings::username() const
{
    return m_store.value(kUsernameKey).toString();
}

void Settings::setUsername(const QString &name)
{
    if (name == username())
        return;
    m_store.setValue(kUsernameKey, name);
    emit usernameChanged();
}

QString Settings::userId() const
{
    return m_store.value(kUserIdKey).toString();
}

void Settings::setUserId(const QString &id)
{
    m_store.setValue(kUserIdKey, id);
}

QString Settings::playbackEngine() const
{
    const QString engine = m_store.value(kEngineKey).toString();
    return engine == QLatin1String("vlc") ? engine : QStringLiteral("mpv");
}

void Settings::setPlaybackEngine(const QString &engine)
{
    m_store.setValue(kEngineKey, engine);
}

QString Settings::toneMapping() const
{
    return m_store.value(kToneMappingKey, QStringLiteral("hable")).toString();
}

QStringList Settings::densityModes()
{
    return {QStringLiteral("compact"), QStringLiteral("comfortable"), QStringLiteral("tv")};
}

QString Settings::densityMode() const
{
    const QString mode = m_store.value(kDensityKey, kDefaultDensity).toString();
    // An unknown value on disk (hand-edited INI, a downgrade) must not leave the
    // UI at an undefined density.
    return densityModes().contains(mode) ? mode : kDefaultDensity;
}

void Settings::setDensityMode(const QString &mode)
{
    if (!densityModes().contains(mode) || mode == densityMode())
        return;
    m_store.setValue(kDensityKey, mode);
    emit densityModeChanged();
}

QStringList Settings::themeAccents()
{
    return {QStringLiteral("projection"), QStringLiteral("emby"), QStringLiteral("jellyfin"),
            QStringLiteral("breeze")};
}

QString Settings::themeAccent() const
{
    const QString accent = m_store.value(kThemeAccentKey, kDefaultAccent).toString();
    return themeAccents().contains(accent) ? accent : kDefaultAccent;
}

void Settings::setThemeAccent(const QString &accent)
{
    if (!themeAccents().contains(accent) || accent == themeAccent())
        return;
    m_store.setValue(kThemeAccentKey, accent);
    emit themeAccentChanged();
}

bool Settings::reducedMotion() const
{
    return m_store.value(kReducedMotionKey, false).toBool();
}

void Settings::setReducedMotion(bool reduced)
{
    if (reduced == reducedMotion())
        return;
    m_store.setValue(kReducedMotionKey, reduced);
    emit reducedMotionChanged();
}

int Settings::volume() const
{
    const int stored = m_store.value(kVolumeKey, kDefaultVolume).toInt();
    return qBound(0, stored, kMaxVolume);
}

void Settings::setVolume(int percent)
{
    const int clamped = qBound(0, percent, kMaxVolume);
    if (clamped == volume())
        return;
    m_store.setValue(kVolumeKey, clamped);
    emit volumeChanged();
}

bool Settings::autoPlayNextEpisode() const
{
    return m_store.value(QStringLiteral("playback/autoPlayNextEpisode"), true).toBool();
}

void Settings::setAutoPlayNextEpisode(bool enabled)
{
    if (enabled == autoPlayNextEpisode())
        return;
    m_store.setValue(QStringLiteral("playback/autoPlayNextEpisode"), enabled);
    emit autoPlayNextEpisodeChanged();
}

int Settings::maxBitrateKbps() const
{
    return qMax(0, m_store.value(QStringLiteral("playback/maxBitrateKbps"), 0).toInt());
}

void Settings::setMaxBitrateKbps(int kbps)
{
    const int clamped = qMax(0, kbps);
    if (clamped == maxBitrateKbps())
        return;
    m_store.setValue(QStringLiteral("playback/maxBitrateKbps"), clamped);
    emit maxBitrateKbpsChanged();
}

QString Settings::playbackMode() const
{
    const QString stored =
        m_store.value(QStringLiteral("playback/mode"), QStringLiteral("auto")).toString();
    if (stored == QLatin1String("directPlay") || stored == QLatin1String("transcode"))
        return stored;
    return QStringLiteral("auto");
}

void Settings::setPlaybackMode(const QString &mode)
{
    if (mode == playbackMode())
        return;
    m_store.setValue(QStringLiteral("playback/mode"), mode);
    emit playbackModeChanged();
}

QStringList Settings::replayGainModes()
{
    return {QStringLiteral("off"), QStringLiteral("track"), QStringLiteral("album")};
}

QString Settings::replayGainMode() const
{
    const QString stored = m_store.value(kReplayGainKey, QStringLiteral("off")).toString();
    // Validated on the way out rather than on the way in, so a hand-edited INI
    // cannot put a value the engine will refuse into the audio chain.
    return replayGainModes().contains(stored) ? stored : QStringLiteral("off");
}

void Settings::setReplayGainMode(const QString &mode)
{
    if (!replayGainModes().contains(mode))
        return;
    // Compared against the RAW stored value rather than the validated getter.
    // With a hand-edited INI that reads back as "off", choosing "Off" would
    // otherwise match and early-return, leaving the value the engine refuses on
    // disk indefinitely — and silently swallowing the write for anything else
    // reading the file.
    if (mode == m_store.value(kReplayGainKey).toString())
        return;
    m_store.setValue(kReplayGainKey, mode);
    emit replayGainModeChanged();
}

bool Settings::backdropEnabled() const
{
    return m_store.value(QStringLiteral("appearance/backdrop"), true).toBool();
}

void Settings::setBackdropEnabled(bool enabled)
{
    if (enabled == backdropEnabled())
        return;
    m_store.setValue(QStringLiteral("appearance/backdrop"), enabled);
    emit backdropChanged();
}

int Settings::backdropOpacity() const
{
    return qBound(0, m_store.value(QStringLiteral("appearance/backdropOpacity"), 18).toInt(), 100);
}

void Settings::setBackdropOpacity(int percent)
{
    const int clamped = qBound(0, percent, 100);
    if (clamped == backdropOpacity())
        return;
    m_store.setValue(QStringLiteral("appearance/backdropOpacity"), clamped);
    emit backdropChanged();
}

bool Settings::backdropKenBurns() const
{
    return m_store.value(QStringLiteral("appearance/backdropKenBurns"), true).toBool();
}

void Settings::setBackdropKenBurns(bool enabled)
{
    if (enabled == backdropKenBurns())
        return;
    m_store.setValue(QStringLiteral("appearance/backdropKenBurns"), enabled);
    emit backdropChanged();
}

QString Settings::subtitleFont() const
{
    return m_store.value(QStringLiteral("subtitles/font")).toString();
}

void Settings::setSubtitleFont(const QString &family)
{
    if (family == subtitleFont())
        return;
    m_store.setValue(QStringLiteral("subtitles/font"), family);
    emit subtitleStyleChanged();
}

int Settings::subtitleScale() const
{
    return qBound(50, m_store.value(QStringLiteral("subtitles/scale"), 100).toInt(), 300);
}

void Settings::setSubtitleScale(int percent)
{
    const int clamped = qBound(50, percent, 300);
    if (clamped == subtitleScale())
        return;
    m_store.setValue(QStringLiteral("subtitles/scale"), clamped);
    emit subtitleStyleChanged();
}

QString Settings::subtitleColor() const
{
    const QString stored =
        m_store.value(QStringLiteral("subtitles/color"), QStringLiteral("#FFFFFF")).toString();
    // Anything that is not a plain #RRGGBB is refused rather than passed to
    // mpv, which would reject the whole property and leave subtitles unstyled.
    static const QRegularExpression hex(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    return hex.match(stored).hasMatch() ? stored.toUpper() : QStringLiteral("#FFFFFF");
}

void Settings::setSubtitleColor(const QString &color)
{
    if (color == subtitleColor())
        return;
    m_store.setValue(QStringLiteral("subtitles/color"), color);
    emit subtitleStyleChanged();
}

int Settings::subtitleBackground() const
{
    return qBound(0, m_store.value(QStringLiteral("subtitles/background"), 0).toInt(), 100);
}

void Settings::setSubtitleBackground(int percent)
{
    const int clamped = qBound(0, percent, 100);
    if (clamped == subtitleBackground())
        return;
    m_store.setValue(QStringLiteral("subtitles/background"), clamped);
    emit subtitleStyleChanged();
}

int Settings::subtitlePosition() const
{
    return qBound(0, m_store.value(QStringLiteral("subtitles/position"), 100).toInt(), 150);
}

void Settings::setSubtitlePosition(int position)
{
    const int clamped = qBound(0, position, 150);
    if (clamped == subtitlePosition())
        return;
    m_store.setValue(QStringLiteral("subtitles/position"), clamped);
    emit subtitleStyleChanged();
}

QString Settings::libraryViewMode(const QString &libraryKey) const
{
    if (libraryKey.isEmpty())
        return {};
    return m_store.value(scopedKey(QStringLiteral("libraryView/%1/mode").arg(libraryKey)))
        .toString();
}

void Settings::setLibraryViewMode(const QString &libraryKey, const QString &mode)
{
    const QString key = scopedKey(QStringLiteral("libraryView/%1/mode").arg(libraryKey));
    if (libraryKey.isEmpty() || key.isEmpty())
        return;
    m_store.setValue(key, mode);
}

int Settings::libraryCardSizeStep(const QString &libraryKey) const
{
    const QString key = scopedKey(QStringLiteral("libraryView/%1/size").arg(libraryKey));
    if (libraryKey.isEmpty() || key.isEmpty())
        return -1;
    bool ok = false;
    const int step = m_store.value(key, -1).toInt(&ok);
    return ok ? step : -1;
}

void Settings::setLibraryCardSizeStep(const QString &libraryKey, int step)
{
    const QString key = scopedKey(QStringLiteral("libraryView/%1/size").arg(libraryKey));
    if (libraryKey.isEmpty() || key.isEmpty())
        return;
    m_store.setValue(key, step);
}

void Settings::rememberTracks(const QString &itemId, const QString &mediaSourceId, int audioId,
                              int subtitleId)
{
    const QString key = scopedKey(QStringLiteral("tracks/%1_%2").arg(itemId, mediaSourceId));
    if (itemId.isEmpty() || key.isEmpty())
        return;
    m_store.setValue(key, QStringLiteral("%1,%2").arg(audioId).arg(subtitleId));
}

bool Settings::hasRememberedTracks(const QString &itemId, const QString &mediaSourceId) const
{
    const QString key = scopedKey(QStringLiteral("tracks/%1_%2").arg(itemId, mediaSourceId));
    if (itemId.isEmpty() || key.isEmpty())
        return false;
    return m_store.contains(key);
}

QPair<int, int> Settings::recalledTracks(const QString &itemId,
                                         const QString &mediaSourceId) const
{
    const QString key = scopedKey(QStringLiteral("tracks/%1_%2").arg(itemId, mediaSourceId));
    if (itemId.isEmpty() || key.isEmpty())
        return {-1, -1};
    const QStringList parts = m_store.value(key).toString().split(QLatin1Char(','));
    if (parts.size() != 2)
        return {-1, -1};
    bool audioOk = false;
    bool subOk = false;
    const int audio = parts[0].toInt(&audioOk);
    const int subtitle = parts[1].toInt(&subOk);
    return {audioOk ? audio : -1, subOk ? subtitle : -1};
}

QString Settings::rememberedVersion(const QString &itemId) const
{
    const QString key = scopedKey(QStringLiteral("versions/%1").arg(itemId));
    if (itemId.isEmpty() || key.isEmpty())
        return {};
    return m_store.value(key).toString();
}

void Settings::rememberVersion(const QString &itemId, const QString &mediaSourceId)
{
    const QString key = scopedKey(QStringLiteral("versions/%1").arg(itemId));
    if (itemId.isEmpty() || key.isEmpty())
        return;
    m_store.setValue(key, mediaSourceId);
}

bool Settings::muted() const
{
    return m_store.value(kMutedKey, false).toBool();
}

void Settings::setMuted(bool muted)
{
    if (muted == this->muted())
        return;
    m_store.setValue(kMutedKey, muted);
    emit mutedChanged();
}

bool Settings::liveUpdatesEnabled() const
{
    return m_store.value(kLiveEnabledKey, true).toBool();
}

void Settings::setLiveUpdatesEnabled(bool enabled)
{
    if (enabled == liveUpdatesEnabled())
        return;
    m_store.setValue(kLiveEnabledKey, enabled);
    emit liveUpdatesEnabledChanged();
}

QList<int> Settings::pollIntervalChoices()
{
    return {15, 30, 60, 120, 300};
}

int Settings::pollIntervalSeconds() const
{
    const int stored = m_store.value(kPollIntervalKey, kDefaultPollSeconds).toInt();
    if (stored <= 0) // missing, non-numeric, or nonsense on disk
        return kDefaultPollSeconds;
    return qBound(kMinPollSeconds, stored, kMaxPollSeconds);
}

void Settings::setPollIntervalSeconds(int seconds)
{
    const int clamped = qBound(kMinPollSeconds, seconds <= 0 ? kDefaultPollSeconds : seconds,
                               kMaxPollSeconds);
    if (clamped == pollIntervalSeconds())
        return;
    m_store.setValue(kPollIntervalKey, clamped);
    emit pollIntervalSecondsChanged();
}

void Settings::setLastPlayback(const QString &itemId, const QString &title, qint64 positionMs)
{
    const QString itemKey = scopedKey(kLastItemKey);
    if (itemKey.isEmpty())
        return;
    m_store.setValue(itemKey, itemId);
    m_store.setValue(scopedKey(kLastTitleKey), title);
    m_store.setValue(scopedKey(kLastPositionKey), positionMs);
    m_store.sync(); // must survive a crash — flush now
}

QVariantMap Settings::lastPlayback() const
{
    const QString itemKey = scopedKey(kLastItemKey);
    if (itemKey.isEmpty())
        return {};
    const QString itemId = m_store.value(itemKey).toString();
    if (itemId.isEmpty())
        return {};
    QVariantMap map;
    map.insert(QStringLiteral("itemId"), itemId);
    map.insert(QStringLiteral("title"), m_store.value(scopedKey(kLastTitleKey)).toString());
    map.insert(QStringLiteral("positionMs"),
               m_store.value(scopedKey(kLastPositionKey)).toLongLong());
    return map;
}

void Settings::clearLastPlayback()
{
    const QString itemKey = scopedKey(kLastItemKey);
    if (itemKey.isEmpty())
        return;
    m_store.remove(itemKey);
    m_store.remove(scopedKey(kLastTitleKey));
    m_store.remove(scopedKey(kLastPositionKey));
    // A clean stop must be as durable as the crash-resume write. Otherwise a
    // power loss immediately after Stop can resurrect an already-cleared item
    // on the next launch.
    m_store.sync();
}

QString Settings::deviceId()
{
    QString id = m_store.value(kDeviceIdKey).toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_store.setValue(kDeviceIdKey, id);
    }
    return id;
}

} // namespace strmqt
