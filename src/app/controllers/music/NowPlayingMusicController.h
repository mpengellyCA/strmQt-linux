#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "server/dto/music/MusicTypes.h"

namespace strmqt {
class ItemActions;
class PlayerController;
} // namespace strmqt

namespace strmqt::music {

class MusicPlayback;
class MusicRepository;
class TrackListModel;

// What the music player shows (Crate spec §7). Reads the queue, the playback
// ticket and the player's state, and hands QML finished display values: the
// "Out of the sleeve" stage, the docked audio bar and the player's side panel
// never reshape queue maps or stream lists themselves.
class NowPlayingMusicController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY trackChanged)
    Q_PROPERTY(QString trackId READ trackId NOTIFY trackChanged)
    Q_PROPERTY(QVariantMap trackItem READ trackItem NOTIFY trackChanged)
    Q_PROPERTY(QString title READ title NOTIFY trackChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY trackChanged)
    Q_PROPERTY(QString artistId READ artistId NOTIFY trackChanged)
    Q_PROPERTY(QString album READ album NOTIFY trackChanged)
    Q_PROPERTY(QString albumId READ albumId NOTIFY trackChanged)
    Q_PROPERTY(QString coverUrl READ coverUrl NOTIFY trackChanged)
    Q_PROPERTY(bool favourite READ favourite NOTIFY favouriteChanged)
    Q_PROPERTY(QString readout READ readout NOTIFY readoutChanged)
    Q_PROPERTY(QString sourceLabel READ sourceLabel NOTIFY sourceLabelChanged)
    Q_PROPERTY(QString sourceKicker READ sourceKicker NOTIFY sourceLabelChanged)
    Q_PROPERTY(QString recordState READ recordState NOTIFY recordStateChanged)
    Q_PROPERTY(QObject *albumTracks READ albumTracks CONSTANT)
    Q_PROPERTY(int currentAlbumRow READ currentAlbumRow NOTIFY currentAlbumRowChanged)
    Q_PROPERTY(QString albumSummary READ albumSummary NOTIFY albumSummaryChanged)
    Q_PROPERTY(QString timeText READ timeText NOTIFY timeTextChanged)
    Q_PROPERTY(bool lyricsAvailable READ lyricsAvailable NOTIFY lyricsChanged)
    Q_PROPERTY(QVariantList lyrics READ lyrics NOTIFY lyricsChanged)
    Q_PROPERTY(bool lyricsTimed READ lyricsTimed NOTIFY lyricsChanged)
    Q_PROPERTY(int currentLyricRow READ currentLyricRow NOTIFY currentLyricRowChanged)

public:
    NowPlayingMusicController(MusicRepository *repository, MusicPlayback *playback,
                              QObject *parent = nullptr);

    // MusicPlayback exposes neither of these, and this controller reads both.
    // Called once, by Application, right after construction.
    void bind(PlayerController *player, ItemActions *actions);

    bool active() const { return m_active; }
    QString trackId() const { return m_trackId; }
    QVariantMap trackItem() const { return m_trackItem; }
    QString title() const { return m_title; }
    QString artist() const { return m_artist; }
    QString artistId() const { return m_artistId; }
    QString album() const { return m_album; }
    QString albumId() const { return m_albumId; }
    QString coverUrl() const { return m_coverUrl; }
    bool favourite() const;
    QString readout() const { return m_readout; }
    QString sourceLabel() const { return m_sourceLabel; }
    QString sourceKicker() const { return m_sourceKicker; }
    QString recordState() const { return m_recordState; }
    QObject *albumTracks() const;
    TrackListModel *trackModel() const { return m_tracks; }
    int currentAlbumRow() const { return m_currentAlbumRow; }
    QString albumSummary() const { return m_albumSummary; }
    QString timeText() const { return m_timeText; }
    bool lyricsAvailable() const { return m_lyricsEnabled && !m_lyrics.isEmpty(); }
    QVariantList lyrics() const { return m_lyricsVariant; }
    // The parser's verdict (emby::parseLyrics): timed lyrics are timed on
    // every line and sorted, untimed ones on none.
    bool lyricsTimed() const { return m_lyricsTimed; }
    int currentLyricRow() const { return m_currentLyricRow; }

    // Plays the current album from `row` of albumTracks, labelled with the album.
    Q_INVOKABLE void playAlbumFrom(int row);
    Q_INVOKABLE void toggleFavourite();

    // caps::kLyricsAvailable is constexpr; tests drive both branches.
    void setLyricsEnabledForTests(bool enabled);

signals:
    // A different album's track is about to be shown. Emitted after coverUrl
    // already holds the new cover and before trackChanged(), so RecordStage can
    // start its slide-in before any binding swaps the sleeve.
    void albumChanging();
    void trackChanged();
    void favouriteChanged();
    void readoutChanged();
    void sourceLabelChanged();
    void recordStateChanged();
    void currentAlbumRowChanged();
    void albumSummaryChanged();
    void timeTextChanged();
    void lyricsChanged();
    void currentLyricRowChanged();

private:
    void refreshTrack();
    void refreshSourceLabel();
    void refreshRecordState();
    void refreshReadout();
    void refreshTime();
    void refreshCurrentAlbumRow();
    void refreshCurrentLyricRow();
    void loadAlbumTracks(const QString &albumId);
    void setAlbumSummary(const QString &summary);
    void loadLyrics();
    void clearLyrics();

    MusicRepository *m_repository = nullptr;
    MusicPlayback *m_playback = nullptr;
    TrackListModel *m_tracks = nullptr;
    QPointer<PlayerController> m_player;
    QPointer<ItemActions> m_actions;

    bool m_active = false;
    QString m_trackId;
    QVariantMap m_trackItem;
    QString m_title;
    QString m_artist;
    QString m_artistId;
    QString m_album;
    QString m_albumId;
    QString m_coverUrl;
    bool m_itemFavourite = false;
    QHash<QString, bool> m_favouriteOverrides;
    QString m_readout;
    QString m_sourceLabel;
    QString m_sourceKicker;
    QString m_recordState = QStringLiteral("stopped");
    QString m_timeText;

    QString m_tracksAlbumId;   // the album asked for
    QString m_loadedAlbumId;   // the album m_tracks actually holds; empty while loading
    quint64 m_albumGeneration = 0;
    int m_currentAlbumRow = -1;
    QString m_albumSummary;

    bool m_lyricsEnabled;
    QString m_lyricsKey;
    quint64 m_lyricsGeneration = 0;
    QList<LyricLine> m_lyrics;
    QVariantList m_lyricsVariant;
    bool m_lyricsTimed = false;
    int m_currentLyricRow = -1;
};

} // namespace strmqt::music
