# Player OSD (ARCHITECTURE.md). The old OSD was a title, a stream-method
# string, a non-interactive bar and two timestamps.
qt_target_qml_sources(strmqt QML_FILES
    ui/player/PlayerOsd.qml
    ui/player/UpNextCard.qml
    ui/player/TrackPanel.qml
    ui/player/ChapterPanel.qml
    ui/player/QueuePanel.qml
    ui/player/StatsOverlay.qml
    ui/player/OsdButtonRow.qml
    ui/player/PlaybackSettingsPanel.qml
    ui/player/NowPlayingPanel.qml
)
