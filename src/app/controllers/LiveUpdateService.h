#pragma once

#include "server/emby/EmbyWebSocket.h"

#include <QElapsedTimer>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

namespace strmqt {

class Settings;
namespace emby {
class EmbyClient;
}

// Owns the Emby event socket, the polling fallback, and the fan-out to the
// controllers (ARCHITECTURE.md). Before this existed the UI was a snapshot taken
// at startup: HomeCtl.refresh() ran twice in the whole app.
//
// Transport is "websocket" while the socket is up, "polling" while it is not
// (which also covers the first connect), and "off" when live updates are
// disabled in Settings or no session exists.
//
// Coalescing: a library scan emits a storm of LibraryChanged messages, so ids
// accumulate into a set behind a debounce timer and one invalidation comes out.
// The debounce has a hard ceiling so a continuous storm still delivers.
//
// Suspension: polling pauses during playback and while the window is unfocused —
// a poll that fights the video decoder is worse than stale data. The socket
// stays connected while suspended (it costs nothing) but its invalidations are
// held and flushed on resume.
class LiveUpdateService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(QString transport READ transport NOTIFY transportChanged)
    Q_PROPERTY(int pollIntervalSeconds READ pollIntervalSeconds WRITE setPollIntervalSeconds NOTIFY
                   pollIntervalSecondsChanged)
    Q_PROPERTY(bool suspended READ suspended WRITE setSuspended NOTIFY suspendedChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)

public:
    LiveUpdateService(emby::EmbyClient *client, Settings *settings, QObject *parent = nullptr);

    bool isConnected() const { return m_connected; }
    QString transport() const { return m_transport; }

    // Persisted through Settings; default 60 s.
    int pollIntervalSeconds() const;
    void setPollIntervalSeconds(int seconds);

    bool suspended() const { return m_suspended; }
    bool enabled() const;
    void setEnabled(bool enabled);

    // Connects the socket (or arms the poll timer) for the client's current
    // session. Safe to call repeatedly: a token change reconnects, an unchanged
    // session is a no-op.
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

    // Manual / on-focus / on-playback-stop refresh. Emits refreshRequested()
    // first — the "the user asked for this" path, which controllers apply
    // immediately — then the ordinary invalidations for every other consumer.
    Q_INVOKABLE void refreshNow();

    // Pauses polling and holds socket invalidations. Drive it from playback
    // activity and window focus; see the class comment.
    Q_INVOKABLE void setSuspended(bool suspended);

    emby::EmbyWebSocket *socket() const { return m_socket; }

    // Test seams: shrink the debounce windows and the poll period so a test runs
    // in milliseconds. Settings deliberately floors the poll period at 15 s.
    void setDebounceForTests(int libraryMs, int userDataMs, int maxDeferralMs);
    void setPollIntervalMsForTests(int intervalMs);

signals:
    void libraryInvalidated(const QStringList &itemIds); // empty = everything
    void userDataInvalidated(const QStringList &itemIds);
    // Richer companion: one QVariantMap per item with itemId / played /
    // favorite / positionTicks / playCount, so a model can be patched in place
    // instead of refetched. Only the socket can produce this; polling cannot.
    void userDataPatched(const QVariantList &entries);
    // The user (or the app on their behalf) asked for a refresh right now.
    void refreshRequested();

    void connectedChanged();
    void transportChanged();
    void pollIntervalSecondsChanged();
    void suspendedChanged();
    void enabledChanged();

private:
    void onSocketConnectedChanged();
    void onLibraryChanged(const QStringList &added, const QStringList &removed,
                          const QStringList &updated);
    void onUserDataEntries(const QList<emby::EmbyWebSocket::UserDataEntry> &entries);
    void flushLibrary();
    void flushUserData();
    void armLibraryFlush();
    void armUserDataFlush();
    void applyTransport();
    void setTransport(const QString &transport);
    void setConnected(bool connected);
    void updatePollTimer();

    emby::EmbyClient *m_client;
    Settings *m_settings;
    emby::EmbyWebSocket *m_socket;

    QTimer m_pollTimer;
    QTimer m_libraryDebounce;
    QTimer m_userDataDebounce;
    // Ceiling on how long a continuous message storm may defer delivery.
    QElapsedTimer m_libraryBurst;
    QElapsedTimer m_userDataBurst;

    QSet<QString> m_pendingLibraryIds;
    QSet<QString> m_pendingUserDataIds;
    bool m_pendingLibraryAll = false; // the burst grew past the point of tracking ids
    bool m_heldWhileSuspended = false;

    QString m_transport = QStringLiteral("off");
    bool m_connected = false;
    bool m_started = false;
    bool m_suspended = false;

    int m_libraryDebounceMs = 750;
    int m_userDataDebounceMs = 250;
    int m_maxDeferralMs = 5000;
    int m_pollOverrideMs = 0; // > 0 replaces the Settings-derived poll period
};

} // namespace strmqt
