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
)
