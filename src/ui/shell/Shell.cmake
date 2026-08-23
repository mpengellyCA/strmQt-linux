# Application chrome (ARCHITECTURE.md). Main.qml is a bare StackView today:
# no header, no back, no search or settings affordance, so a first-run mouse
# user can sign in and is then functionally stuck.
qt_target_qml_sources(strmqt QML_FILES
    ui/shell/TopBar.qml
    ui/shell/NavRail.qml
    ui/shell/PageHeader.qml
    ui/shell/ShortcutSheet.qml
    ui/shell/CommandPalette.qml
    ui/shell/EmptyState.qml
    ui/shell/LoadingState.qml
    ui/shell/FilterBar.qml
    ui/shell/UpdateBanner.qml
    ui/shell/MiniPlayer.qml
)
