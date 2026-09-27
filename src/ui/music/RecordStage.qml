// Bound: the groove Repeater's delegate reaches out to this file's ids
// (disc.diameter), which is only well-defined (and only lint-clean) with
// bound component behaviour.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import QtQuick.Window
import StrmQt

// RecordStage: the "Out of the sleeve" stage (Crate spec §7.2).
//
// A square sleeve with a record behind it. While music plays the record sits
// 45% out of the sleeve and turns at 33⅓ rpm; a pause lets it run down; a new
// album slides the record in, cross-fades the sleeve and slides it out again;
// a stopped session puts it away. Everything here is presentation: the state
// comes from NowPlayingMusicCtl.recordState and the album change from its
// albumChanging() signal, which the owner forwards to changeAlbum().
//
// One rotating layer. The disc, its grooves and the circle-cropped label are a
// single layer-enabled item, so a turn re-composites one texture instead of
// re-rendering the cover and the mask every frame. The animator runs only while
// it can be seen: the page is current (`live`), the window is shown, and motion
// is allowed; otherwise it is stopped, not hidden.
Item {
    id: stage

    property string coverUrl: ""
    // "playing" | "paused" | "buffering" | "stopped". Not `state`: that is
    // Item's own state-machine property.
    property string recordState: "stopped"
    // Settings → Appearance → Animate record. Off: a still record, slid out.
    property bool animate: true
    // The page holding this is the current page.
    property bool live: true
    // The shared sleeve is in the air (Main.qml's SleeveFlight). The stage keeps
    // its geometry for the flight to land on but draws nothing, and the record
    // stays in until the flight lands.
    property bool holdIn: false
    property real sleeveSize: Theme.scale(420)

    readonly property bool motion: stage.animate && !Theme.reducedMotion
    readonly property bool windowShown: stage.Window.visibility !== Window.Hidden
                                        && stage.Window.visibility !== Window.Minimized
    readonly property bool spinning: stage.motion && stage.live && stage.windowShown
                                     && stage.recordState === "playing"
                                     && !stage.holdIn && internal.phase === ""
    readonly property alias phase: internal.phase
    readonly property alias shownCover: internal.shownCover
    readonly property alias slide: disc.slide
    readonly property bool changing: changeSequence.running
    readonly property bool turning: spin.running
    readonly property bool settling: settle.running
    readonly property real sleeveRadius: Theme.crateSleeveRadius

    // The transition's large endpoint, in `target`'s coordinates. Empty before
    // layout, so Main.qml's poll waits one more frame.
    function sleeveRect(target: Item): rect {
        if (sleeve.width <= 0 || sleeve.height <= 0)
            return Qt.rect(0, 0, 0, 0);
        const corner = sleeve.mapToItem(target, 0, 0);
        return Qt.rect(corner.x, corner.y, sleeve.width, sleeve.height);
    }

    // A different album is about to play. Called before coverUrl moves.
    function changeAlbum(newCoverUrl: string): void {
        if (!stage.motion || stage.recordState === "stopped" || stage.holdIn) {
            changeSequence.stop();
            internal.phase = "";
            stage.swapCover(newCoverUrl);
            return;
        }
        internal.pendingCover = newCoverUrl;
        changeSequence.restart();
    }

    function swapCover(url: string): void {
        if (url === internal.shownCover)
            return;
        // A blank incoming cover (no art, or a change straight to nothing)
        // has nothing to fade from underneath: keeping the old art around as
        // `previousCover` would leave it showing forever, since nothing ever
        // starts a load that could clear it. Only stash the outgoing cover
        // when there is a new one to fade in over it.
        internal.previousCover = url !== "" ? internal.shownCover : "";
        internal.shownCover = url;
    }

    function syncSpin(): void {
        if (stage.spinning) {
            settle.stop();
            const from = disc.rotation % 360;
            disc.rotation = from;
            spin.from = from;
            spin.to = from + 360;
            spin.restart();
            return;
        }
        if (!spin.running)
            return;
        spin.stop(); // an animator writes the angle it reached back on stop
        if (stage.recordState === "paused" && stage.motion) {
            settle.from = disc.rotation;
            settle.to = disc.rotation + 40;
            settle.restart();
        }
    }

    implicitWidth: Math.round(stage.sleeveSize * 1.45)
    implicitHeight: stage.sleeveSize

    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("Record")

    onSpinningChanged: stage.syncSpin()
    onMotionChanged: {
        if (!stage.motion)
            settle.stop();
    }
    onCoverUrlChanged: {
        if (!changeSequence.running)
            stage.swapCover(stage.coverUrl);
    }
    Component.onCompleted: {
        internal.shownCover = stage.coverUrl;
        stage.syncSpin();
    }

    QtObject {
        id: internal

        property string phase: ""
        property string shownCover: ""
        property string previousCover: ""
        property string pendingCover: ""
    }

    SequentialAnimation {
        id: changeSequence

        ScriptAction { script: internal.phase = "in" }
        PauseAnimation { duration: Theme.animSlow }
        ScriptAction { script: stage.swapCover(internal.pendingCover) }
        PauseAnimation { duration: Theme.animNormalMs }
        ScriptAction {
            script: {
                internal.phase = "";
                stage.swapCover(stage.coverUrl);
            }
        }
    }

    RotationAnimator {
        id: spin

        target: disc
        duration: 1800 // 33⅓ rpm
        loops: Animation.Infinite
    }

    RotationAnimator {
        id: settle

        target: disc
        duration: 600
        easing.type: Theme.easeStandard
    }

    // ── Shadow ──────────────────────────────────────────────────────────────
    // A flat caster behind the sleeve, for the reasons SleeveFlight.qml gives:
    // an effect on the sleeve itself would pad and inset its texture.
    Item {
        x: sleeve.x
        y: sleeve.y
        width: sleeve.width
        height: sleeve.height
        visible: !stage.holdIn

        Rectangle {
            id: shadowCaster

            anchors.fill: parent
            radius: stage.sleeveRadius
            color: Theme.shadowColor
            layer.enabled: true
        }

        MultiEffect {
            anchors.fill: parent
            source: shadowCaster
            autoPaddingEnabled: true
            shadowEnabled: true
            shadowColor: Theme.shadowColor
            shadowBlur: Theme.crateSleeveElevation.blur
            shadowVerticalOffset: Theme.crateSleeveElevation.y
            shadowOpacity: Theme.crateSleeveElevation.opacity
        }
    }

    // ── The record ──────────────────────────────────────────────────────────
    Item {
        id: disc

        readonly property real diameter: Math.round(stage.sleeveSize * 0.95)
        readonly property real slideTarget: (stage.recordState === "stopped" || stage.holdIn
                                             || internal.phase === "in") ? 0 : 0.45
        property real slide: disc.slideTarget

        x: Math.round((stage.sleeveSize - disc.diameter) / 2 + disc.slide * stage.sleeveSize)
        y: Math.round((stage.sleeveSize - disc.diameter) / 2)
        width: disc.diameter
        height: disc.diameter
        visible: !stage.holdIn
        layer.enabled: true
        layer.smooth: true

        Behavior on slide {
            enabled: stage.motion
            NumberAnimation {
                duration: Theme.animSlow
                easing.type: Theme.easeStandard
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: Theme.ground
            border.width: 1
            border.color: Theme.hairline
        }

        // Grooves: a few hairline rings between the rim and the label.
        Repeater {
            model: 4

            Rectangle {
                id: groove

                required property int index

                readonly property real size: disc.diameter * (0.9 - groove.index * 0.12)

                anchors.centerIn: parent
                width: groove.size
                height: groove.size
                radius: groove.size / 2
                color: "transparent"
                border.width: 1
                border.color: Theme.hairline
                opacity: 0.6
            }
        }

        // The label: the cover, circle-cropped.
        Item {
            id: label

            readonly property real size: Math.round(disc.diameter * 0.36)

            anchors.centerIn: parent
            width: label.size
            height: label.size

            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: Theme.surfaceRaisedColor
            }

            Item {
                id: labelArt

                anchors.fill: parent
                visible: false
                layer.enabled: true

                StrmImage {
                    anchors.fill: parent
                    source: internal.shownCover
                }
            }

            Rectangle {
                id: labelMask

                anchors.fill: parent
                radius: width / 2
                visible: false
                layer.enabled: true
                layer.smooth: true
            }

            MultiEffect {
                anchors.fill: parent
                source: labelArt
                maskEnabled: true
                maskSource: labelMask
                maskThresholdMin: 0.5
                maskSpreadAtMin: 1.0
            }

            // Spindle hole.
            Rectangle {
                anchors.centerIn: parent
                width: Math.max(4, Math.round(label.size * 0.06))
                height: width
                radius: width / 2
                color: Theme.ground
            }
        }
    }

    // ── The sleeve ──────────────────────────────────────────────────────────
    Rectangle {
        id: sleeve

        width: stage.sleeveSize
        height: stage.sleeveSize
        radius: stage.sleeveRadius
        clip: true
        color: Theme.surfaceRaisedColor
        // Geometry stays for the flight to land on; the square itself is in the air.
        opacity: stage.holdIn ? 0 : 1

        // The outgoing cover under the incoming one: StrmImage starts a new
        // source at opacity 0 and fades in once it is Ready, which is the
        // cross-fade. `previousCover` is cleared (never just hidden) once it
        // is no longer needed — the incoming cover finished its fade, or
        // failed outright — so a blank or broken cover never leaves stale
        // art showing, and nothing stays decoded past its own cross-fade.
        StrmImage {
            id: previousCoverImage
            objectName: "previousCoverImage"

            anchors.fill: parent
            source: internal.previousCover
            fadeDuration: 0
        }

        StrmImage {
            id: currentCoverImage
            objectName: "currentCoverImage"

            anchors.fill: parent
            source: internal.shownCover
            fadeDuration: Theme.animSlow

            onStatusChanged: {
                if (currentCoverImage.status === Image.Ready)
                    previousCoverClear.restart();
                else if (currentCoverImage.status === Image.Error)
                    internal.previousCover = "";
            }
        }

        Timer {
            id: previousCoverClear

            interval: Theme.animSlow
            onTriggered: internal.previousCover = ""
        }
    }
}
