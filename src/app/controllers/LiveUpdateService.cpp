#include "LiveUpdateService.h"

#include "core/Log.h"
#include "core/Settings.h"
#include "server/emby/EmbyClient.h"

#include <QVariantMap>

namespace strmqt {

namespace {

// Above this many distinct ids in one burst, tracking them is pointless: the
// consumers refresh everything anyway and the set only costs memory.
constexpr int kIdTrackingCeiling = 200;

const auto kTransportWebSocket = QStringLiteral("websocket");
const auto kTransportPolling = QStringLiteral("polling");
const auto kTransportOff = QStringLiteral("off");

} // namespace

LiveUpdateService::LiveUpdateService(emby::EmbyClient *client, Settings *settings, QObject *parent)
    : QObject(parent), m_client(client), m_settings(settings),
      m_socket(new emby::EmbyWebSocket(this))
{
    m_pollTimer.setSingleShot(false);
    connect(&m_pollTimer, &QTimer::timeout, this, [this] {
        if (m_suspended)
            return; // belt and braces: updatePollTimer() already stops it
        qCDebug(logApp) << "live: polling tick";
        emit libraryInvalidated({});
        emit userDataInvalidated({});
    });

    m_libraryDebounce.setSingleShot(true);
    connect(&m_libraryDebounce, &QTimer::timeout, this, &LiveUpdateService::flushLibrary);
    m_userDataDebounce.setSingleShot(true);
    connect(&m_userDataDebounce, &QTimer::timeout, this, &LiveUpdateService::flushUserData);

    connect(m_socket, &emby::EmbyWebSocket::connectedChanged, this,
            &LiveUpdateService::onSocketConnectedChanged);
    connect(m_socket, &emby::EmbyWebSocket::libraryChanged, this,
            &LiveUpdateService::onLibraryChanged);
    connect(m_socket, &emby::EmbyWebSocket::userDataEntriesChanged, this,
            &LiveUpdateService::onUserDataEntries);

    if (m_settings) {
        connect(m_settings, &Settings::pollIntervalSecondsChanged, this,
                [this] { updatePollTimer(); });
        connect(m_settings, &Settings::liveUpdatesEnabledChanged, this, [this] {
            emit enabledChanged();
            if (m_started)
                start();
        });
    }
}

bool LiveUpdateService::enabled() const
{
    return m_settings ? m_settings->liveUpdatesEnabled() : true;
}

void LiveUpdateService::setEnabled(bool enabled)
{
    if (!m_settings || enabled == m_settings->liveUpdatesEnabled())
        return;
    m_settings->setLiveUpdatesEnabled(enabled); // → liveUpdatesEnabledChanged → restart
}

int LiveUpdateService::pollIntervalSeconds() const
{
    return m_settings ? m_settings->pollIntervalSeconds() : 60;
}

void LiveUpdateService::setPollIntervalSeconds(int seconds)
{
    if (!m_settings)
        return;
    const int before = m_settings->pollIntervalSeconds();
    m_settings->setPollIntervalSeconds(seconds);
    if (m_settings->pollIntervalSeconds() != before)
        emit pollIntervalSecondsChanged();
}

void LiveUpdateService::setDebounceForTests(int libraryMs, int userDataMs, int maxDeferralMs)
{
    m_libraryDebounceMs = qMax(0, libraryMs);
    m_userDataDebounceMs = qMax(0, userDataMs);
    m_maxDeferralMs = qMax(m_libraryDebounceMs, maxDeferralMs);
}

void LiveUpdateService::setPollIntervalMsForTests(int intervalMs)
{
    m_pollOverrideMs = qMax(0, intervalMs);
    updatePollTimer();
}

void LiveUpdateService::start()
{
    m_started = true;

    if (!enabled()) {
        qCInfo(logApp) << "live: updates disabled in settings";
        m_socket->disconnectFromServer();
        m_pollTimer.stop();
        setConnected(false);
        setTransport(kTransportOff);
        return;
    }
    if (!m_client || !m_client->hasSession()) {
        // Not an error: the app starts here and SessionController calls back.
        m_socket->disconnectFromServer();
        m_pollTimer.stop();
        setConnected(false);
        setTransport(kTransportOff);
        return;
    }

    // EmbyClient has no deviceId() getter, and adding one is another agent's
    // file; Settings is the source of truth for the device identity anyway —
    // Application feeds the same value into EmbyClient::setDeviceId().
    const QString deviceId = m_settings ? m_settings->deviceId() : QString();
    m_socket->connectToServer(m_client->baseUrl(), m_client->accessToken(), deviceId);
    applyTransport();
}

void LiveUpdateService::stop()
{
    m_started = false;
    m_socket->disconnectFromServer();
    m_pollTimer.stop();
    m_libraryDebounce.stop();
    m_userDataDebounce.stop();
    m_pendingLibraryIds.clear();
    m_pendingUserDataIds.clear();
    m_pendingLibraryAll = false;
    m_heldWhileSuspended = false;
    setConnected(false);
    setTransport(kTransportOff);
}

void LiveUpdateService::refreshNow()
{
    // Order matters: refreshRequested() lets a controller start an
    // apply-immediately refresh, and the invalidations that follow are then
    // dropped by that controller as already-in-flight work.
    emit refreshRequested();
    emit libraryInvalidated({});
    emit userDataInvalidated({});
}

void LiveUpdateService::setSuspended(bool suspended)
{
    if (m_suspended == suspended)
        return;
    m_suspended = suspended;
    emit suspendedChanged();
    updatePollTimer();

    if (m_suspended) {
        // A debounce armed in the moment before suspension is still armed, and
        // it fires during it: the library grid reloads mid-playback, which is
        // the one thing suspension exists to prevent. Deferred rather than
        // dropped — the pending id sets are untouched, so resume delivers them
        // exactly as it does for a message that arrives while already
        // suspended (armLibraryFlush()). Dropping would be silent data loss:
        // the socket does not resend, so the grid would stay wrong until the
        // next unrelated invalidation.
        if (m_libraryDebounce.isActive() || m_userDataDebounce.isActive()) {
            m_libraryDebounce.stop();
            m_userDataDebounce.stop();
            m_heldWhileSuspended = true;
        }
        return;
    }

    // Deliver whatever the socket reported while we were quiet.
    if (m_heldWhileSuspended) {
        m_heldWhileSuspended = false;
        flushLibrary();
        flushUserData();
    }
}

void LiveUpdateService::onSocketConnectedChanged()
{
    setConnected(m_socket->isConnected());
    applyTransport();
}

void LiveUpdateService::applyTransport()
{
    if (!m_started || !enabled() || !m_client || !m_client->hasSession()) {
        m_pollTimer.stop();
        setTransport(kTransportOff);
        return;
    }
    if (m_socket->isConnected()) {
        m_pollTimer.stop();
        setTransport(kTransportWebSocket);
        return;
    }
    // The socket is down or still connecting: polling covers the gap.
    setTransport(kTransportPolling);
    updatePollTimer();
}

void LiveUpdateService::updatePollTimer()
{
    const bool shouldPoll = m_started && enabled() && !m_suspended && !m_socket->isConnected() &&
                            m_client && m_client->hasSession();
    if (!shouldPoll) {
        m_pollTimer.stop();
        return;
    }
    const int intervalMs = m_pollOverrideMs > 0 ? m_pollOverrideMs : pollIntervalSeconds() * 1000;
    if (m_pollTimer.interval() != intervalMs)
        m_pollTimer.setInterval(intervalMs);
    if (!m_pollTimer.isActive())
        m_pollTimer.start();
}

void LiveUpdateService::onLibraryChanged(const QStringList &added, const QStringList &removed,
                                         const QStringList &updated)
{
    if (!m_pendingLibraryAll) {
        for (const QStringList *list : {&added, &removed, &updated})
            for (const QString &id : *list)
                m_pendingLibraryIds.insert(id);
        if (m_pendingLibraryIds.size() > kIdTrackingCeiling) {
            m_pendingLibraryAll = true;
            m_pendingLibraryIds.clear();
        }
    }
    // A LibraryChanged with nothing in it still means "something moved".
    if (added.isEmpty() && removed.isEmpty() && updated.isEmpty())
        m_pendingLibraryAll = true;

    armLibraryFlush();
}

void LiveUpdateService::onUserDataEntries(
    const QList<emby::EmbyWebSocket::UserDataEntry> &entries)
{
    if (entries.isEmpty())
        return;

    // The patch is free — no network, no layout change — so it goes out
    // immediately even while suspended: a played/favourite tick that lags the
    // phone by a poll interval is exactly the bug this milestone exists to fix.
    QVariantList payload;
    payload.reserve(entries.size());
    for (const emby::EmbyWebSocket::UserDataEntry &entry : entries) {
        QVariantMap map;
        map.insert(QStringLiteral("itemId"), entry.itemId);
        map.insert(QStringLiteral("played"), entry.played);
        map.insert(QStringLiteral("favorite"), entry.favorite);
        map.insert(QStringLiteral("positionTicks"), entry.playbackPositionTicks);
        map.insert(QStringLiteral("playCount"), entry.playCount);
        payload.append(map);
        m_pendingUserDataIds.insert(entry.itemId);
    }
    emit userDataPatched(payload);

    armUserDataFlush();
}

void LiveUpdateService::armLibraryFlush()
{
    if (m_suspended) {
        m_heldWhileSuspended = true;
        m_libraryDebounce.stop();
        return;
    }
    if (!m_libraryBurst.isValid() || !m_libraryDebounce.isActive())
        m_libraryBurst.start();
    // A steady stream of messages must not defer delivery forever.
    if (m_libraryBurst.elapsed() >= m_maxDeferralMs) {
        flushLibrary();
        return;
    }
    m_libraryDebounce.start(m_libraryDebounceMs);
}

void LiveUpdateService::armUserDataFlush()
{
    if (m_suspended) {
        m_heldWhileSuspended = true;
        m_userDataDebounce.stop();
        return;
    }
    if (!m_userDataBurst.isValid() || !m_userDataDebounce.isActive())
        m_userDataBurst.start();
    if (m_userDataBurst.elapsed() >= m_maxDeferralMs) {
        flushUserData();
        return;
    }
    m_userDataDebounce.start(m_userDataDebounceMs);
}

void LiveUpdateService::flushLibrary()
{
    m_libraryDebounce.stop();
    m_libraryBurst.invalidate();
    if (!m_pendingLibraryAll && m_pendingLibraryIds.isEmpty())
        return;

    const QStringList ids = m_pendingLibraryAll
                                ? QStringList()
                                : QStringList(m_pendingLibraryIds.cbegin(),
                                              m_pendingLibraryIds.cend());
    m_pendingLibraryIds.clear();
    m_pendingLibraryAll = false;
    qCDebug(logApp) << "live: library invalidated," << ids.size() << "id(s)";
    emit libraryInvalidated(ids);
}

void LiveUpdateService::flushUserData()
{
    m_userDataDebounce.stop();
    m_userDataBurst.invalidate();
    if (m_pendingUserDataIds.isEmpty())
        return;

    const QStringList ids(m_pendingUserDataIds.cbegin(), m_pendingUserDataIds.cend());
    m_pendingUserDataIds.clear();
    qCDebug(logApp) << "live: user data invalidated," << ids.size() << "id(s)";
    emit userDataInvalidated(ids);
}

void LiveUpdateService::setTransport(const QString &transport)
{
    if (m_transport == transport)
        return;
    m_transport = transport;
    qCInfo(logApp) << "live: transport is" << m_transport;
    emit transportChanged();
}

void LiveUpdateService::setConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    emit connectedChanged();
}

} // namespace strmqt
