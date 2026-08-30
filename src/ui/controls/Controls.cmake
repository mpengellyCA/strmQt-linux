# Shared control library (ARCHITECTURE.md). Every control is simultaneously
# pointer- and focus-driven; retrofitting mouse support a second time is
# exactly how the prototype ended up with two MouseAreas app-wide.
# One value shared by every horizontal shelf on a page, so a vertical move can
# keep the column it started in (NavigationColumn.qml).
set_source_files_properties(ui/controls/NavigationColumn.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_target_qml_sources(strmqt QML_FILES
    ui/controls/NavigationColumn.qml
    ui/controls/FocusRing.qml
    ui/controls/StrmIcon.qml
    ui/controls/StrmButton.qml
    ui/controls/StrmIconButton.qml
    ui/controls/StrmChip.qml
    ui/controls/StrmSwitch.qml
    ui/controls/StrmSearchField.qml
    ui/controls/StrmSlider.qml
    ui/controls/StrmScrollBar.qml
    ui/controls/StrmTooltip.qml
    ui/controls/StrmCard.qml
    ui/controls/StrmImage.qml
    ui/controls/StrmAvatar.qml
    ui/controls/StrmRail.qml
    ui/controls/StrmGrid.qml
    ui/controls/NavigationFocusRestorer.qml
    ui/controls/StrmMenu.qml
    ui/controls/ItemMenu.qml
    ui/controls/StrmSelect.qml
    ui/controls/PersonCard.qml
    ui/controls/LinkChip.qml
    ui/controls/StrmToast.qml
    ui/controls/StrmToastHost.qml
    ui/controls/StrmSkeleton.qml
    ui/controls/StrmPanel.qml
    ui/controls/StrmTabBar.qml
    ui/controls/TrackRow.qml
    ui/controls/TrackTable.qml
    ui/controls/PlaylistPicker.qml
    ui/controls/SelectionBar.qml
    ui/controls/MappedShortcut.qml
)
