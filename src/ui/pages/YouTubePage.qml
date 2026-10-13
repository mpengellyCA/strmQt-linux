import QtQuick
import StrmQt

// The YouTube destination: YouTube's ten-foot interface, hosted as one more
// page of the shell. The rail and the header stay where they are on every
// other page, and Left off YouTube's own left edge opens the rail exactly as
// Left off a library grid does.
//
// The page does not own the web view. There is one, it belongs to the shell
// (Main.qml youtubeHost), and it sits in this page's `slot` while the page is
// up and in the mini player while a video plays on under another page — the
// arrangement the film's video plane has with the player page. So leaving
// this page does not end what YouTube is playing, and neither does the
// navigation stack evicting it.
//
// A build made with -DSTRMQT_WITH_YOUTUBE=OFF has no view to host, and this
// page says so instead of failing to load.
FocusScope {
    id: page

    objectName: "youtubePage"

    // Where the shell's web view goes while this page is the one on top.
    readonly property Item slot: viewSlot
    // The shell's view once it is in the slot; the keyboard is passed to it.
    property Item view: null

    readonly property bool available: typeof YouTubeAvailable !== "undefined" && YouTubeAvailable

    function seatFocus(): void {
        if (page.view !== null && page.activeFocus && !page.view.activeFocus)
            page.view.forceActiveFocus()
    }

    onActiveFocusChanged: page.seatFocus()
    onViewChanged: page.seatFocus()

    // The view arrives in the slot a moment after the page takes the keyboard.
    Connections {
        target: page.view
        function onParentChanged() { Qt.callLater(page.seatFocus) }
    }

    Rectangle {
        anchors.fill: parent
        color: "#0f0f0f"
    }

    Item {
        id: viewSlot

        anchors.fill: parent
        visible: page.available
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
