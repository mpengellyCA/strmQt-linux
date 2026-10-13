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
    // Which of the two this page is showing: "tv", YouTube's ten-foot
    // interface, or "music", YouTube Music. One page for both — it is a
    // slot and a background either way — and the shell tells them apart.
    property string service: "tv"

    readonly property bool inBuild: typeof YouTubeAvailable !== "undefined" && YouTubeAvailable
    // Switched on in Settings. A page for one that is off can still be
    // reached — it may be an entry in the history — and says so.
    readonly property bool switchedOn: typeof Prefs === "undefined" || Prefs === null
        || (page.service === "music" ? Prefs.youtubeMusicEnabled : Prefs.youtubeEnabled)
    readonly property bool available: page.inBuild && page.switchedOn

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
        color: page.service === "music" ? "#030303" : "#0f0f0f"
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
        iconName: page.service === "music" ? "youtube-music" : "youtube"
        headline: page.inBuild
                  ? (page.service === "music" ? qsTr("YouTube Music is turned off")
                                              : qsTr("YouTube is turned off"))
                  : (page.service === "music" ? qsTr("YouTube Music is not part of this build")
                                              : qsTr("YouTube is not part of this build"))
        body: page.inBuild ? qsTr("It can be turned back on in Settings, under Server.")
                           : qsTr("This copy of StrmQt was built without its web engine.")
    }
}
