import QtQuick

// The YouTube destination: YouTube's ten-foot interface, hosted as one more
// page of the shell. The rail and the header stay where they are on every
// other page, and Left off YouTube's own left edge opens the rail exactly as
// Left off a library grid does.
//
// The web view is loaded by URL (ui/youtube/YouTubeView.qml), so a build made
// with -DSTRMQT_WITH_YOUTUBE=OFF lacks the file and this page says so instead
// of failing to load. It is also not built for the page self-test: Chromium
// will not start as root in a CI container, and the self-test is about this
// app's QML, not about reaching youtube.com.
FocusScope {
    id: page

    objectName: "youtubePage"

    // Where to open: the TV interface's front page, or one video / search
    // (Main.qml openYouTube()).
    property url address: "https://www.youtube.com/tv"

    signal railRequested
    signal exitRequested

    readonly property bool available: typeof YouTubeAvailable !== "undefined" && YouTubeAvailable
                                      && !(typeof SelfTest !== "undefined" && SelfTest)
    readonly property bool onScreen: page.visible && page.enabled

    // One engine or the other is heard, never both: a YouTube video that
    // starts pauses the film or the record, and leaving the page (which keeps
    // it alive in history) pauses YouTube.
    onOnScreenChanged: {
        if (!page.onScreen && viewLoader.item)
            viewLoader.item.pausePlayback()
    }

    Connections {
        target: PlayerCtl
        enabled: viewLoader.item !== null
        function onPausedChanged() {
            if (PlayerCtl.active && !PlayerCtl.paused)
                viewLoader.item.pausePlayback()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#0f0f0f"
    }

    Loader {
        id: viewLoader

        anchors.fill: parent
        focus: true
        active: page.available
        source: page.available ? Qt.resolvedUrl("../youtube/YouTubeView.qml") : ""
        onLoaded: {
            viewLoader.item.address = page.address
            viewLoader.item.focus = true
        }
    }

    Connections {
        target: viewLoader.item
        ignoreUnknownSignals: true
        function onLeftEdgeReached() { page.railRequested() }
        function onExitRequested() { page.exitRequested() }
        function onPlaybackStarted() {
            if (PlayerCtl.active && !PlayerCtl.paused)
                PlayerCtl.togglePause()
        }
    }

    EmptyState {
        anchors.fill: parent
        visible: !page.available
        focus: !page.available
        iconName: "youtube"
        headline: qsTr("YouTube is not part of this build")
        body: qsTr("This copy of StrmQt was built without its web engine.")
    }
}
