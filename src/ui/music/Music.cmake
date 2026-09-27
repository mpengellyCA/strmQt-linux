# Crate: the music dialect of the control library (spec §2). Its own fragment,
# so each music phase appends its controls without touching src/CMakeLists.txt.
qt_target_qml_sources(strmqt QML_FILES
    ui/music/CrateHeading.qml
    ui/music/CrateKicker.qml
    ui/music/CrateBadge.qml
    ui/music/CoverCollage.qml
    ui/music/CrateSleeve.qml
    ui/music/CratePortrait.qml
    ui/music/StationTile.qml
    ui/music/GenreBinTile.qml
    ui/music/SectionStrip.qml
    ui/music/ShelfError.qml
    ui/music/CrateShelf.qml
    ui/music/FilterPill.qml
    ui/music/GenrePicker.qml
    ui/music/CrateDividers.qml
)

# Phase 4: the album, artist and playlist page controls.
qt_target_qml_sources(strmqt
    QML_FILES
        ui/music/LinerNotes.qml
        ui/music/CrateTrackTable.qml
)

# Phase 5: the player
qt_target_qml_sources(strmqt QML_FILES
    ui/music/RecordStage.qml
    ui/music/MusicPlayerPanel.qml
    ui/music/MusicNowPlaying.qml
)
