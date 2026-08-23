import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import StrmQt

// PlaybackSettingsPanel — everything behind the OSD's gear icon
// (ARCHITECTURE.md).
//
// What it replaces: an inline sheet inside PlayerOsd.qml with a speed dropdown
// and two ± steppers. What it is now: the things a player actually has to be
// able to change mid-film, split across three tabs so a 440 px sheet never
// becomes a wall of controls.
//
//   Playback   speed (0.25×–4×), audio delay, subtitle delay   — all live
//   Quality    which version of the item, bitrate cap, mode
//   Subtitles  size, colour, background, vertical position     — all live
//
// Three things are worth stating because they are the parts that would
// otherwise read as bugs:
//
//  1. A bitrate cap and the playback mode travel to the server inside the
//     DeviceProfile attached to PlaybackInfo, so they are decided when a stream
//     STARTS. Changing either mid-item does nothing to the frame on screen.
//     Rather than let that look broken, the Quality tab says so, in a strip
//     that is part of the layout and not a toast the user can miss.
//  2. Switching version is the opposite: PlayerController restarts the stream
//     immediately, at the position you were at.
//  3. Subtitle appearance is written straight to Prefs. Settings emits
//     subtitleStyleChanged and PlayerController pushes the look to the engine
//     from there — so a drag is visible on the frame under the panel, and no
//     applySubtitleStyle() call belongs in this file.
//
// Hover never moves focus (ARCHITECTURE.md): every control here comes from
// src/ui/controls, all of which separate the two, and nothing in this file
// calls forceActiveFocus() from a HoverHandler.
FocusScope {
    id: panel

    signal closeRequested

    // 0 = playback, 1 = quality, 2 = subtitles.
    property int tab: 0

    // ── Player state ────────────────────────────────────────────────────────
    // Every read is guarded. The OSD is assembled from files several agents
    // extend in parallel, and a binding that throws takes the whole sheet down;
    // a missing property has to read as "unavailable", which is what a disabled
    // or hidden control already means.
    readonly property real speed: {
        const value = PlayerCtl.playbackSpeed;
        return value !== undefined ? Number(value) : 1.0;
    }
    readonly property int audioDelay: {
        const value = PlayerCtl.audioDelayMs;
        return value !== undefined ? Number(value) : 0;
    }
    readonly property int subtitleDelay: {
        const value = PlayerCtl.subtitleDelayMs;
        return value !== undefined ? Number(value) : 0;
    }

    readonly property var sources: {
        const list = PlayerCtl.sources;
        return (list !== undefined && list !== null) ? list : [];
    }
    readonly property int sourceCount: {
        const value = PlayerCtl.sourceCount;
        return value !== undefined ? Number(value) : panel.sources.length;
    }
    readonly property int sourceIndex: {
        const value = PlayerCtl.sourceIndex;
        return value !== undefined ? Number(value) : -1;
    }
    readonly property var currentSource: {
        const map = PlayerCtl.currentSource;
        return (map !== undefined && map !== null) ? map : ({});
    }
    // A single-version item has nothing to pick between, so it gets no picker
    // at all rather than a dropdown with one entry in it.
    readonly property bool hasVersions: panel.sourceCount > 1

    // ── Preferences ─────────────────────────────────────────────────────────
    readonly property int maxBitrateKbps: {
        const value = Prefs.maxBitrateKbps;
        return value !== undefined ? Number(value) : 0;
    }
    readonly property string playbackMode: {
        const value = Prefs.playbackMode;
        return value !== undefined ? String(value) : "auto";
    }
    readonly property int subtitleScale: {
        const value = Prefs.subtitleScale;
        return value !== undefined ? Number(value) : 100;
    }
    readonly property string subtitleColor: {
        const value = Prefs.subtitleColor;
        return (value !== undefined && String(value).length > 0) ? String(value) : "#FFFFFF";
    }
    readonly property int subtitleBackground: {
        const value = Prefs.subtitleBackground;
        return value !== undefined ? Number(value) : 0;
    }
    readonly property int subtitlePosition: {
        const value = Prefs.subtitlePosition;
        return value !== undefined ? Number(value) : 100;
    }

    // ── Option ladders ──────────────────────────────────────────────────────
    // { text, value } throughout, so no handler ever parses its own label back
    // into a number.
    readonly property var speedOptions: [
        ({ "text": "0.25×", "value": 0.25 }),
        ({ "text": "0.5×", "value": 0.5 }),
        ({ "text": "0.75×", "value": 0.75 }),
        ({ "text": qsTr("Normal (1×)"), "value": 1.0 }),
        ({ "text": "1.25×", "value": 1.25 }),
        ({ "text": "1.5×", "value": 1.5 }),
        ({ "text": "1.75×", "value": 1.75 }),
        ({ "text": "2×", "value": 2.0 }),
        ({ "text": "3×", "value": 3.0 }),
        ({ "text": "4×", "value": 4.0 })
    ]

    // The standard Emby ladder. 0 is not "a very large number": a cap makes the
    // server transcode, so "no cap" has to be its own entry.
    readonly property var bitrateOptions: [
        ({ "text": qsTr("Auto — no cap"), "value": 0 }),
        ({ "text": "120 Mbps", "value": 120000 }),
        ({ "text": "40 Mbps", "value": 40000 }),
        ({ "text": "20 Mbps", "value": 20000 }),
        ({ "text": "10 Mbps", "value": 10000 }),
        ({ "text": "4 Mbps", "value": 4000 }),
        ({ "text": "2 Mbps", "value": 2000 })
    ]

    readonly property var colorOptions: [
        ({ "text": qsTr("White"), "value": "#FFFFFF" }),
        ({ "text": qsTr("Soft white"), "value": "#E8E4DC" }),
        ({ "text": qsTr("Yellow"), "value": "#FFE066" }),
        ({ "text": qsTr("Cyan"), "value": "#8AE0FF" }),
        ({ "text": qsTr("Green"), "value": "#8CE08C" }),
        ({ "text": qsTr("Grey"), "value": "#BFBFBF" }),
        ({ "text": qsTr("Black"), "value": "#000000" })
    ]

    readonly property var versionOptions: {
        const out = [];
        for (let i = 0; i < panel.sources.length; ++i)
            out.push({ "text": panel.versionLabel(panel.sources[i], i), "value": i });
        return out;
    }

    // ── Index lookups ───────────────────────────────────────────────────────
    readonly property int speedIndex: {
        for (let i = 0; i < panel.speedOptions.length; ++i) {
            if (Math.abs(Number(panel.speedOptions[i].value) - panel.speed) < 0.001)
                return i;
        }
        return -1;
    }

    readonly property int bitrateIndex: {
        for (let i = 0; i < panel.bitrateOptions.length; ++i) {
            if (Number(panel.bitrateOptions[i].value) === panel.maxBitrateKbps)
                return i;
        }
        return -1;
    }

    readonly property int colorIndex: {
        const wanted = panel.subtitleColor.toUpperCase();
        for (let i = 0; i < panel.colorOptions.length; ++i) {
            if (String(panel.colorOptions[i].value).toUpperCase() === wanted)
                return i;
        }
        return -1;
    }

    // ── Formatting ──────────────────────────────────────────────────────────
    function formatSpeed(value: real): string {
        return value.toFixed(2) + "×";
    }

    function formatBitrate(bitsPerSecond: real): string {
        if (bitsPerSecond <= 0)
            return "";
        const mbps = bitsPerSecond / 1000000;
        return mbps >= 10 ? Math.round(mbps) + " Mbps" : mbps.toFixed(1) + " Mbps";
    }

    function formatSize(bytes: real): string {
        if (bytes <= 0)
            return "";
        const gb = bytes / 1073741824;
        return gb >= 1 ? gb.toFixed(1) + " GB" : Math.round(bytes / 1048576) + " MB";
    }

    // What distinguishes one version of an item from another: the server's own
    // name first, then the numbers a user would actually choose on.
    function versionLabel(source, index): string {
        if (source === undefined || source === null)
            return qsTr("Version %1").arg(index + 1);
        const bits = [];
        const name = source.displayName !== undefined ? String(source.displayName) : "";
        if (name.length > 0)
            bits.push(name);
        const resolution = source.resolutionLabel !== undefined
                         ? String(source.resolutionLabel) : "";
        // displayName already falls back to the resolution, so it is only worth
        // adding when the server gave a real name and this is extra detail.
        if (resolution.length > 0 && name.indexOf(resolution) < 0)
            bits.push(resolution);
        const rate = panel.formatBitrate(source.bitrate !== undefined ? Number(source.bitrate) : 0);
        if (rate.length > 0)
            bits.push(rate);
        if (bits.length === 0)
            return qsTr("Version %1").arg(index + 1);
        return bits.join("  ·  ");
    }

    // The mono detail line under the picker: what the version you are on
    // actually is, in the terms the stats overlay uses.
    function sourceSummary(source): string {
        if (source === undefined || source === null)
            return "";
        const bits = [];
        if (source.container !== undefined && String(source.container).length > 0)
            bits.push(String(source.container).toUpperCase());
        if (source.resolutionLabel !== undefined && String(source.resolutionLabel).length > 0)
            bits.push(String(source.resolutionLabel));
        const video = source.videoStream;
        if (video !== undefined && video !== null && video.codec !== undefined
                && String(video.codec).length > 0)
            bits.push(String(video.codec).toUpperCase());
        if (source.isHdr === true)
            bits.push("HDR");
        const size = panel.formatSize(source.size !== undefined ? Number(source.size) : 0);
        if (size.length > 0)
            bits.push(size);
        const audio = source.audioStreams;
        const subs = source.subtitleStreams;
        if (audio !== undefined && audio !== null && subs !== undefined && subs !== null)
            bits.push(qsTr("%1 audio, %2 sub").arg(audio.length).arg(subs.length));
        return bits.join("  ·  ");
    }

    // ── Verbs ───────────────────────────────────────────────────────────────
    function applySpeed(value: real): void {
        PlayerCtl.setPlaybackSpeed(Math.max(0.25, Math.min(4.0, value)));
    }

    // Steps along the ladder rather than by a fixed increment: the ladder is
    // deliberately not linear, and ±0.25 all the way through 3× and 4× would be
    // a lot of clicking for no extra control.
    function stepSpeed(direction: int): void {
        let index = panel.speedIndex;
        if (index < 0) {
            // Off-ladder — something else set the speed. Land on the nearest
            // rung in the direction asked for.
            index = 0;
            for (let i = 0; i < panel.speedOptions.length; ++i) {
                if (Number(panel.speedOptions[i].value) <= panel.speed)
                    index = i;
            }
            if (direction > 0 && Number(panel.speedOptions[index].value) < panel.speed)
                index = Math.min(panel.speedOptions.length - 1, index + 1);
        } else {
            index += direction;
        }
        index = Math.max(0, Math.min(panel.speedOptions.length - 1, index));
        panel.applySpeed(Number(panel.speedOptions[index].value));
    }

    function resetPlayback(): void {
        panel.applySpeed(1.0);
        PlayerCtl.setAudioDelayMs(0);
        PlayerCtl.setSubtitleDelayMs(0);
    }

    function resetSubtitleLook(): void {
        Prefs.subtitleScale = 100;
        Prefs.subtitleColor = "#FFFFFF";
        Prefs.subtitleBackground = 0;
        Prefs.subtitlePosition = 100;
    }

    readonly property bool playbackIsDefault: Math.abs(panel.speed - 1.0) < 0.001
                                              && panel.audioDelay === 0
                                              && panel.subtitleDelay === 0
    readonly property bool subtitleLookIsDefault: panel.subtitleScale === 100
                                                  && panel.subtitleColor.toUpperCase() === "#FFFFFF"
                                                  && panel.subtitleBackground === 0
                                                  && panel.subtitlePosition === 100

    // Esc closes the sheet; PlayerPage only stops playback once nothing is open.
    Keys.onEscapePressed: event => {
        panel.closeRequested();
        event.accepted = true;
    }

    // ── Reusable rows ───────────────────────────────────────────────────────
    // Inline components rather than files: they are private layout, and putting
    // them in src/ui/controls would imply a contract the rest of the app can
    // rely on. They are self-contained by construction — an inline component
    // cannot see the enclosing document's ids, which is the discipline that
    // keeps them honest.

    // Small uppercase mono section marker (ARCHITECTURE.md).
    component SectionLabel: Text {
        color: Theme.textTertiary
        font.family: Theme.fontMono
        font.pixelSize: Theme.fontCaption
        font.capitalization: Font.AllUppercase
        font.letterSpacing: Theme.trackLabel * Theme.fontCaption
        elide: Text.ElideRight
    }

    component HintText: Text {
        color: Theme.textTertiary
        font.family: Theme.fontBody
        font.pixelSize: Theme.fontCaption
        wrapMode: Text.WordWrap
    }

    // Label, live mono readout, and a slider under both. The slider is the only
    // tab stop in the row, and it owns Left/Right, so the vertical chain
    // through the sheet is never broken by a control that eats the arrow it
    // needs to pass on.
    component SliderRow: Item {
        id: sliderRow

        property string label: ""
        property string valueText: ""
        property real from: 0
        property real to: 100
        property real stepSize: 1
        property real value: 0
        property Item navUp: null
        property Item navDown: null

        // The tab stop, for callers building the vertical chain.
        readonly property alias focusItem: rowSlider

        signal valueRequested(real requested)

        implicitHeight: rowLabel.implicitHeight + rowSlider.height

        Text {
            id: rowLabel

            anchors.left: parent.left
            anchors.top: parent.top
            anchors.right: rowValue.left
            anchors.rightMargin: Theme.spacingTight
            text: sliderRow.label
            color: Theme.textSecondaryColor
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
        }

        Text {
            id: rowValue

            anchors.right: parent.right
            anchors.baseline: rowLabel.baseline
            text: sliderRow.valueText
            color: Theme.textPrimaryColor
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontSmall
        }

        StrmSlider {
            id: rowSlider

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: rowLabel.bottom
            height: Theme.scale(30)
            // This is a settings sheet, not a progress line: the knob is the
            // drag affordance and it stays visible at rest.
            showKnobOnHoverOnly: false
            from: sliderRow.from
            to: sliderRow.to
            stepSize: sliderRow.stepSize
            value: sliderRow.value

            KeyNavigation.up: sliderRow.navUp
            KeyNavigation.down: sliderRow.navDown

            // Both, deliberately: `moved` is what makes a drag visible on the
            // frame underneath, and `committed` catches the wheel and the
            // key-release path, neither of which ends with a `moved`.
            onMoved: dragged => sliderRow.valueRequested(dragged)
            onCommitted: dragged => sliderRow.valueRequested(dragged)
        }
    }

    // A ± stepper for exact values with a slider under it for coarse dragging.
    // The buttons sit on the label line rather than flanking the slider: put
    // them left and right of it and Right from the minus button lands in the
    // slider, which owns Left/Right, and the plus button becomes unreachable by
    // arrow key.
    component DelayRow: Item {
        id: delayRow

        property string label: ""
        property int value: 0
        property int step: 50
        property int minValue: -5000
        property int maxValue: 5000
        property string minusTooltip: ""
        property string plusTooltip: ""
        property Item navUp: null
        property Item navDown: null

        readonly property alias focusItem: delayMinus
        readonly property alias tailItem: delaySlider

        signal delayRequested(int ms)

        implicitHeight: delayHead.height + delaySlider.height

        function bump(delta: int): void {
            delayRow.delayRequested(Math.max(delayRow.minValue,
                                             Math.min(delayRow.maxValue, delayRow.value + delta)));
        }

        function snap(raw: real): int {
            const stepped = Math.round(raw / delayRow.step) * delayRow.step;
            return Math.max(delayRow.minValue, Math.min(delayRow.maxValue, stepped));
        }

        Item {
            id: delayHead

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: Theme.controlHeight

            Text {
                anchors.left: parent.left
                anchors.right: stepper.left
                anchors.rightMargin: Theme.spacingTight
                anchors.verticalCenter: parent.verticalCenter
                text: delayRow.label
                color: Theme.textSecondaryColor
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
            }

            Row {
                id: stepper

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.scale(4)

                StrmIconButton {
                    id: delayMinus

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(30)
                    iconName: "minus"
                    tooltip: delayRow.minusTooltip
                    enabled: delayRow.value > delayRow.minValue
                    onClicked: delayRow.bump(-delayRow.step)

                    KeyNavigation.up: delayRow.navUp
                    KeyNavigation.right: delayPlus
                    KeyNavigation.down: delaySlider
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.scale(78)
                    horizontalAlignment: Text.AlignHCenter
                    // Signed and mono: a delay readout that reflows as it
                    // crosses zero is unreadable while you are nudging it.
                    text: (delayRow.value > 0 ? "+" : "") + delayRow.value + " ms"
                    color: delayRow.value === 0 ? Theme.textSecondaryColor : Theme.accentColor
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSmall
                }

                StrmIconButton {
                    id: delayPlus

                    anchors.verticalCenter: parent.verticalCenter
                    size: Theme.scale(30)
                    iconName: "plus"
                    tooltip: delayRow.plusTooltip
                    enabled: delayRow.value < delayRow.maxValue
                    onClicked: delayRow.bump(delayRow.step)

                    KeyNavigation.up: delayRow.navUp
                    KeyNavigation.left: delayMinus
                    KeyNavigation.down: delaySlider
                }
            }
        }

        StrmSlider {
            id: delaySlider

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: delayHead.bottom
            height: Theme.scale(30)
            showKnobOnHoverOnly: false
            from: delayRow.minValue
            to: delayRow.maxValue
            stepSize: delayRow.step
            value: delayRow.value

            KeyNavigation.up: delayMinus
            KeyNavigation.down: delayRow.navDown

            onMoved: dragged => delayRow.delayRequested(delayRow.snap(dragged))
            onCommitted: dragged => delayRow.delayRequested(delayRow.snap(dragged))
        }
    }

    // ── Surface ─────────────────────────────────────────────────────────────
    StrmPanel {
        id: surface

        anchors.fill: parent
        padding: Theme.spacingValue
        elevation: 3

        // Header, tab bar and the scroller are the body Column's three
        // children, so the scroller's height is the panel minus the two
        // paddings, those two siblings and the two gaps between them.
        Item {
            id: headerRow

            width: parent.width
            height: Theme.controlHeight

            Text {
                anchors.left: parent.left
                anchors.right: closeButton.left
                anchors.rightMargin: Theme.spacingTight
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Playback settings")
                color: Theme.textPrimaryColor
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            StrmIconButton {
                id: closeButton

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                iconName: "close"
                round: true
                tooltip: qsTr("Close")
                shortcut: "Esc"
                onClicked: panel.closeRequested()

                // Esc closes the sheet from anywhere, but a gamepad has to be
                // able to reach the button itself, so it sits above the tabs in
                // the vertical chain rather than only in the Tab order.
                KeyNavigation.down: tabs
            }
        }

        StrmTabBar {
            id: tabs

            width: parent.width
            focus: true
            tabs: [
                ({ "text": qsTr("Playback") }),
                ({ "text": qsTr("Quality") }),
                ({ "text": qsTr("Subtitles") })
            ]
            currentIndex: panel.tab

            KeyNavigation.up: closeButton
            // Down lands on the first control of whichever tab is showing.
            KeyNavigation.down: panel.tab === 0 ? speedMinus
                              : panel.tab === 1 ? (panel.hasVersions ? versionSelect : bitrateSelect)
                              : sizeRow.focusItem

            onTabSelected: index => {
                panel.tab = index;
                scroller.contentY = 0;
            }
        }

        Item {
            id: contentArea

            width: parent.width
            height: Math.max(Theme.scale(120),
                             panel.height - 2 * surface.padding - headerRow.height
                             - tabs.height - 2 * Theme.spacingTight)

            Flickable {
                id: scroller

                anchors.fill: parent
                clip: true
                contentWidth: scroller.width
                contentHeight: content.implicitHeight
                boundsBehavior: Flickable.StopAtBounds

                ScrollBar.vertical: StrmScrollBar {}

                // Tab and arrow keys can land on a control that is scrolled out
                // of sight; without this the sheet looks like it stopped
                // responding. Ancestry is checked rather than assumed, so a
                // focused menu popup — which lives in the window overlay, not
                // in this Flickable — never drags the view around.
                readonly property Item focusedItem: scroller.Window.activeFocusItem as Item

                function isInside(item: Item): bool {
                    let walker = item;
                    while (walker !== null) {
                        if (walker === content)
                            return true;
                        walker = walker.parent;
                    }
                    return false;
                }

                function revealFocus(): void {
                    const item = scroller.focusedItem;
                    if (item === null || !scroller.isInside(item))
                        return;
                    const margin = Theme.spacingTight;
                    const top = item.mapToItem(content, 0, 0).y;
                    const bottom = top + item.height;
                    const limit = Math.max(0, scroller.contentHeight - scroller.height);
                    if (top - margin < scroller.contentY)
                        scroller.contentY = Math.max(0, top - margin);
                    else if (bottom + margin > scroller.contentY + scroller.height)
                        scroller.contentY = Math.min(limit, bottom + margin - scroller.height);
                }

                onFocusedItemChanged: scroller.revealFocus()

                Column {
                    id: content

                    width: scroller.width
                    spacing: Theme.spacingValue

                    // ── Playback ────────────────────────────────────────────
                    Column {
                        width: parent.width
                        visible: panel.tab === 0
                        spacing: Theme.spacingTight

                        SectionLabel {
                            width: parent.width
                            text: qsTr("Speed")
                        }

                        Item {
                            width: parent.width
                            height: Theme.controlHeight

                            Row {
                                id: speedGroup

                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: Theme.spacingTight

                                StrmIconButton {
                                    id: speedMinus

                                    anchors.verticalCenter: parent.verticalCenter
                                    iconName: "minus"
                                    tooltip: qsTr("Slower")
                                    enabled: panel.speed > 0.25
                                    onClicked: panel.stepSpeed(-1)

                                    KeyNavigation.up: tabs
                                    KeyNavigation.right: speedSelect
                                    KeyNavigation.down: audioRow.focusItem
                                }

                                StrmSelect {
                                    id: speedSelect

                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Math.max(Theme.scale(120),
                                                    speedGroup.width - speedMinus.width
                                                    - speedPlus.width - 2 * speedGroup.spacing)
                                    model: panel.speedOptions
                                    currentIndex: panel.speedIndex
                                    // An off-ladder speed (set by a shortcut, or
                                    // restored from a previous session) still
                                    // has to be legible, so it becomes the
                                    // placeholder instead of reading as
                                    // "nothing chosen".
                                    placeholder: panel.formatSpeed(panel.speed)

                                    KeyNavigation.up: tabs
                                    KeyNavigation.left: speedMinus
                                    KeyNavigation.right: speedPlus
                                    KeyNavigation.down: audioRow.focusItem

                                    // StrmSelect writes its own currentIndex on
                                    // activation, which drops the binding above.
                                    // Re-establishing it is what keeps the
                                    // control showing the player's speed rather
                                    // than the last thing that was clicked.
                                    onActivated: index => {
                                        panel.applySpeed(Number(panel.speedOptions[index].value));
                                        speedSelect.currentIndex = Qt.binding(() => panel.speedIndex);
                                    }
                                }

                                StrmIconButton {
                                    id: speedPlus

                                    anchors.verticalCenter: parent.verticalCenter
                                    iconName: "plus"
                                    tooltip: qsTr("Faster")
                                    enabled: panel.speed < 4.0
                                    onClicked: panel.stepSpeed(1)

                                    KeyNavigation.up: tabs
                                    KeyNavigation.left: speedSelect
                                    KeyNavigation.down: audioRow.focusItem
                                }
                            }
                        }

                        HintText {
                            width: parent.width
                            text: qsTr("0.25× to 4×. Applies to the frame on screen straight away.")
                        }

                        Item {
                            width: 1
                            height: Theme.spacingTight
                        }

                        SectionLabel {
                            width: parent.width
                            text: qsTr("A/V sync")
                        }

                        DelayRow {
                            id: audioRow

                            width: parent.width
                            label: qsTr("Audio delay")
                            value: panel.audioDelay
                            minusTooltip: qsTr("Audio 50 ms earlier")
                            plusTooltip: qsTr("Audio 50 ms later")
                            navUp: speedMinus
                            navDown: subtitleSyncRow.focusItem

                            onDelayRequested: ms => PlayerCtl.setAudioDelayMs(ms)
                        }

                        DelayRow {
                            id: subtitleSyncRow

                            width: parent.width
                            label: qsTr("Subtitle delay")
                            value: panel.subtitleDelay
                            minusTooltip: qsTr("Subtitles 50 ms earlier")
                            plusTooltip: qsTr("Subtitles 50 ms later")
                            navUp: audioRow.tailItem
                            navDown: playbackReset

                            onDelayRequested: ms => PlayerCtl.setSubtitleDelayMs(ms)
                        }

                        HintText {
                            width: parent.width
                            text: qsTr("A positive delay plays that track later. Both take effect as you drag.")
                        }

                        StrmButton {
                            id: playbackReset

                            variant: "ghost"
                            iconName: "refresh"
                            text: qsTr("Reset speed and sync")
                            enabled: !panel.playbackIsDefault
                            onClicked: panel.resetPlayback()

                            KeyNavigation.up: subtitleSyncRow.tailItem
                        }
                    }

                    // ── Quality ─────────────────────────────────────────────
                    Column {
                        width: parent.width
                        visible: panel.tab === 1
                        spacing: Theme.spacingTight

                        SectionLabel {
                            width: parent.width
                            visible: panel.hasVersions
                            text: qsTr("Version")
                        }

                        StrmSelect {
                            id: versionSelect

                            width: parent.width
                            visible: panel.hasVersions
                            model: panel.versionOptions
                            currentIndex: panel.sourceIndex
                            placeholder: qsTr("Choose a version")

                            KeyNavigation.up: tabs
                            KeyNavigation.down: bitrateSelect

                            // The re-binding matters most here: the controller
                            // refuses to switch to an unplayable source, and a
                            // picker left showing a version that was rejected
                            // is worse than one that snaps back.
                            onActivated: index => {
                                PlayerCtl.setPreferredSource(index);
                                versionSelect.currentIndex = Qt.binding(() => panel.sourceIndex);
                            }
                        }

                        Text {
                            width: parent.width
                            visible: panel.hasVersions && text.length > 0
                            text: panel.sourceSummary(panel.currentSource)
                            color: Theme.textSecondaryColor
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fontCaption
                            elide: Text.ElideRight
                        }

                        HintText {
                            width: parent.width
                            visible: panel.hasVersions
                            text: qsTr("Switching version restarts the stream where you are now.")
                        }

                        Item {
                            width: 1
                            height: Theme.spacingTight
                            visible: panel.hasVersions
                        }

                        SectionLabel {
                            width: parent.width
                            text: qsTr("Maximum bitrate")
                        }

                        StrmSelect {
                            id: bitrateSelect

                            width: parent.width
                            model: panel.bitrateOptions
                            currentIndex: panel.bitrateIndex
                            // A cap written by another surface need not sit on
                            // this ladder; show the real number rather than an
                            // empty control.
                            placeholder: panel.maxBitrateKbps > 0
                                       ? qsTr("%1 Mbps").arg((panel.maxBitrateKbps / 1000).toFixed(1))
                                       : qsTr("Auto — no cap")

                            KeyNavigation.up: panel.hasVersions ? versionSelect : tabs
                            KeyNavigation.down: modeAuto

                            onActivated: index => {
                                Prefs.maxBitrateKbps = Number(panel.bitrateOptions[index].value);
                                bitrateSelect.currentIndex = Qt.binding(() => panel.bitrateIndex);
                            }
                        }

                        // The one thing in this sheet that would otherwise read
                        // as a bug: the cap above and the mode below are part of
                        // the DeviceProfile sent when a stream STARTS, so
                        // nothing about the frame on screen changes when they
                        // move. Said in the layout, not in a toast.
                        Rectangle {
                            width: parent.width
                            height: noteRow.implicitHeight + Theme.spacingValue
                            radius: Theme.radiusChip
                            color: Theme.surfaceRaisedColor
                            border.width: 1
                            border.color: Theme.hairline

                            Row {
                                id: noteRow

                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: Theme.spacingTight
                                spacing: Theme.spacingTight

                                StrmIcon {
                                    name: "info"
                                    size: Theme.scale(16)
                                    color: Theme.warningColor
                                }

                                Text {
                                    width: noteRow.width - Theme.scale(16) - noteRow.spacing
                                    text: qsTr("Bitrate and mode travel with the request that starts a stream, so they apply from the next item — not to what is playing now.")
                                    color: Theme.textSecondaryColor
                                    font.family: Theme.fontBody
                                    font.pixelSize: Theme.fontCaption
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        Item {
                            width: 1
                            height: Theme.spacingTight
                        }

                        SectionLabel {
                            width: parent.width
                            text: qsTr("Playback mode")
                        }

                        Flow {
                            width: parent.width
                            spacing: Theme.spacingTight

                            StrmChip {
                                id: modeAuto

                                text: qsTr("Auto")
                                checked: panel.playbackMode === "auto"
                                onToggled: Prefs.playbackMode = "auto"

                                KeyNavigation.up: bitrateSelect
                                KeyNavigation.right: modeDirect
                            }

                            StrmChip {
                                id: modeDirect

                                text: qsTr("Direct play")
                                checked: panel.playbackMode === "directPlay"
                                onToggled: Prefs.playbackMode = "directPlay"

                                KeyNavigation.up: bitrateSelect
                                KeyNavigation.left: modeAuto
                                KeyNavigation.right: modeTranscode
                            }

                            StrmChip {
                                id: modeTranscode

                                text: qsTr("Transcode")
                                checked: panel.playbackMode === "transcode"
                                onToggled: Prefs.playbackMode = "transcode"

                                KeyNavigation.up: bitrateSelect
                                KeyNavigation.left: modeDirect
                            }
                        }

                        HintText {
                            width: parent.width
                            text: qsTr("Direct play refuses to transcode and fails instead; transcode makes the server re-encode even when it need not.")
                        }
                    }

                    // ── Subtitles ───────────────────────────────────────────
                    Column {
                        width: parent.width
                        visible: panel.tab === 2
                        spacing: Theme.spacingTight

                        SectionLabel {
                            width: parent.width
                            text: qsTr("Appearance")
                        }

                        // A live sample, so a drag is legible even when the
                        // frame underneath happens to have no subtitle on it.
                        Rectangle {
                            width: parent.width
                            height: Theme.scale(64)
                            radius: Theme.radiusChip
                            color: Theme.ground
                            border.width: 1
                            border.color: Theme.hairline
                            clip: true

                            Rectangle {
                                anchors.centerIn: sampleText
                                width: sampleText.implicitWidth + Theme.spacingTight
                                height: sampleText.implicitHeight + Theme.scale(4)
                                radius: Theme.scale(2)
                                color: Qt.rgba(0, 0, 0, panel.subtitleBackground / 100)
                            }

                            Text {
                                id: sampleText

                                anchors.horizontalCenter: parent.horizontalCenter
                                // mpv's sub-pos axis: 0 is the top of the
                                // frame, 100 the usual place near the bottom.
                                // The sample follows the same axis so the
                                // slider means exactly one thing.
                                y: Math.round((parent.height - sampleText.implicitHeight)
                                              * Math.max(0, Math.min(1, panel.subtitlePosition / 150)))
                                width: parent.width - Theme.spacingValue
                                horizontalAlignment: Text.AlignHCenter
                                text: qsTr("Sample subtitle")
                                color: panel.subtitleColor
                                font.family: Theme.fontBody
                                font.pixelSize: Math.round(Theme.fontSmall * panel.subtitleScale / 100)
                                elide: Text.ElideRight
                                // Mirrors the engine, which drops the outline
                                // once the box is opaque enough to carry the
                                // text on its own.
                                style: panel.subtitleBackground > 50 ? Text.Normal : Text.Outline
                                styleColor: Theme.ground
                            }
                        }

                        SliderRow {
                            id: sizeRow

                            width: parent.width
                            label: qsTr("Text size")
                            valueText: panel.subtitleScale + " %"
                            from: 50
                            to: 300
                            stepSize: 5
                            value: panel.subtitleScale
                            navUp: tabs
                            navDown: colorSelect

                            onValueRequested: requested => Prefs.subtitleScale = Math.round(requested)
                        }

                        Item {
                            width: parent.width
                            height: Theme.controlHeight

                            Text {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Colour")
                                color: Theme.textSecondaryColor
                                font.family: Theme.fontBody
                                font.pixelSize: Theme.fontSmall
                            }

                            StrmSelect {
                                id: colorSelect

                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.min(Theme.scale(200), parent.width * 0.6)
                                model: panel.colorOptions
                                currentIndex: panel.colorIndex
                                // A colour set elsewhere shows as its hex code
                                // rather than as an empty control.
                                placeholder: panel.subtitleColor

                                KeyNavigation.up: sizeRow.focusItem
                                KeyNavigation.down: backgroundRow.focusItem

                                onActivated: index => {
                                    Prefs.subtitleColor = String(panel.colorOptions[index].value);
                                    colorSelect.currentIndex = Qt.binding(() => panel.colorIndex);
                                }
                            }
                        }

                        SliderRow {
                            id: backgroundRow

                            width: parent.width
                            label: qsTr("Background")
                            valueText: panel.subtitleBackground + " %"
                            from: 0
                            to: 100
                            stepSize: 5
                            value: panel.subtitleBackground
                            navUp: colorSelect
                            navDown: positionRow.focusItem

                            onValueRequested: requested => Prefs.subtitleBackground = Math.round(requested)
                        }

                        HintText {
                            width: parent.width
                            text: qsTr("At 0 the text is drawn with an outline and no box behind it.")
                        }

                        SliderRow {
                            id: positionRow

                            width: parent.width
                            label: qsTr("Vertical position")
                            valueText: String(panel.subtitlePosition)
                            from: 0
                            to: 150
                            stepSize: 5
                            value: panel.subtitlePosition
                            navUp: backgroundRow.focusItem
                            navDown: subtitleReset

                            onValueRequested: requested => Prefs.subtitlePosition = Math.round(requested)
                        }

                        HintText {
                            width: parent.width
                            text: qsTr("0 is the top of the frame, 100 the usual place near the bottom, 150 below it.")
                        }

                        StrmButton {
                            id: subtitleReset

                            variant: "ghost"
                            iconName: "refresh"
                            text: qsTr("Reset appearance")
                            enabled: !panel.subtitleLookIsDefault
                            onClicked: panel.resetSubtitleLook()

                            KeyNavigation.up: positionRow.focusItem
                        }
                    }
                }
            }
        }
    }
}
