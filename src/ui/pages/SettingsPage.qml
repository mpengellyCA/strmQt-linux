// Bound: the section rail and the binding table both use nested Repeaters whose
// delegates reach out to this file's ids, which is only well-defined — and only
// lint-clean — with bound component behaviour.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// Settings (ARCHITECTURE.md). A section rail on the left, the selected section's
// panels on the right — so a preference is one keystroke or one click away
// instead of a scroll through everything that is not it.
//
// The row vocabulary (InfoRow, SettingRow, SectionNote, the subtitle preview,
// the binding table) lives in ui/settings/SettingsSections.qml; this file is the
// list of preferences and the wiring behind them.
//
// Rules this page holds itself to:
//   · every control shows the stored value on arrival, and NOTHING writes a
//     preference merely by being shown — writes happen in activation handlers
//     only;
//   · hover never moves keyboard focus (ARCHITECTURE.md);
//   · a preference that does not take effect immediately says so, out loud,
//     next to the control (the bitrate cap is the big one).
FocusScope {
    id: page

    objectName: "settingsPage"

    // ── Where the preferences live ─────────────────────────────────────────
    // `Prefs` is a context property (main.cpp), so it cannot be import-checked.
    // The try/catch is the only way to ask "is it there?" without a
    // ReferenceError, and it keeps this page loadable in a harness that does
    // not publish it.
    readonly property var prefs: {
        try {
            return Prefs;
        } catch (err) {
            return null;
        }
    }

    readonly property bool prefsAvailable: page.prefs !== null && page.prefs !== undefined

    readonly property string unavailableHint: qsTr("Unavailable: the settings object is not exposed to QML.")

    // ── Sections ───────────────────────────────────────────────────────────
    readonly property var sections: [
        {
            "key": "server",
            "title": qsTr("Server"),
            "icon": "cast"
        },
        {
            "key": "appearance",
            "title": qsTr("Appearance"),
            "icon": "grid"
        },
        {
            "key": "playback",
            "title": qsTr("Playback"),
            "icon": "play"
        },
        {
            "key": "subtitles",
            "title": qsTr("Subtitles"),
            "icon": "subtitles"
        },
        {
            "key": "live",
            "title": qsTr("Live updates"),
            "icon": "refresh"
        },
        {
            "key": "input",
            "title": qsTr("Input"),
            "icon": "settings"
        },
        {
            "key": "about",
            "title": qsTr("About"),
            "icon": "info"
        }
    ]

    property int currentSection: 0

    readonly property string currentKey: (page.currentSection >= 0
                                          && page.currentSection < page.sections.length)
                                         ? page.sections[page.currentSection].key : ""

    // ── Vocabularies ───────────────────────────────────────────────────────
    readonly property var accentOptions: [
        {
            "text": qsTr("Projection Booth"),
            "value": "projection"
        },
        {
            "text": qsTr("Emby Green"),
            "value": "emby"
        },
        {
            "text": qsTr("Jellyfin Purple"),
            "value": "jellyfin"
        },
        {
            "text": qsTr("Breeze Blue"),
            "value": "breeze"
        }
    ]

    readonly property var densityOptions: [
        {
            "text": qsTr("Compact"),
            "value": "compact"
        },
        {
            "text": qsTr("Comfortable"),
            "value": "comfortable"
        },
        {
            "text": qsTr("TV"),
            "value": "tv"
        }
    ]

    readonly property var engineOptions: [
        {
            "text": qsTr("mpv"),
            "value": "mpv"
        },
        {
            "text": qsTr("VLC"),
            "value": "vlc"
        }
    ]

    // ARCHITECTURE.md. Values are kbps, matching Settings::maxBitrateKbps; 0 is
    // "no cap", which is deliberately not spelled as a very large number —
    // a cap makes the server transcode, and asking it to transcode to 200 Mbps
    // is worse than asking it not to.
    readonly property var bitrateOptions: [
        {
            "text": qsTr("Auto (no cap)"),
            "value": 0
        },
        {
            "text": qsTr("120 Mbps"),
            "value": 120000
        },
        {
            "text": qsTr("40 Mbps"),
            "value": 40000
        },
        {
            "text": qsTr("20 Mbps"),
            "value": 20000
        },
        {
            "text": qsTr("10 Mbps"),
            "value": 10000
        },
        {
            "text": qsTr("4 Mbps"),
            "value": 4000
        },
        {
            "text": qsTr("2 Mbps"),
            "value": 2000
        }
    ]

    readonly property var playbackModeOptions: [
        {
            "text": qsTr("Automatic"),
            "value": "auto"
        },
        {
            "text": qsTr("Direct play only"),
            "value": "directPlay"
        },
        {
            "text": qsTr("Always transcode"),
            "value": "transcode"
        }
    ]

    // Mirrors Settings::pollIntervalChoices(), which is a static C++ function
    // and therefore not callable from QML. Anything else on disk is clamped by
    // Settings rather than rejected, so a hand-edited INI still shows up here
    // as the nearest sane value.
    readonly property var pollOptions: [
        {
            "text": qsTr("15 seconds"),
            "value": 15
        },
        {
            "text": qsTr("30 seconds"),
            "value": 30
        },
        {
            "text": qsTr("1 minute"),
            "value": 60
        },
        {
            "text": qsTr("2 minutes"),
            "value": 120
        },
        {
            "text": qsTr("5 minutes"),
            "value": 300
        }
    ]

    // The colours anyone actually uses for subtitles. mpv takes #RRGGBB and
    // Settings refuses anything else, so these are stored verbatim.
    readonly property var subtitleColors: [
        {
            "text": qsTr("White"),
            "value": "#FFFFFF"
        },
        {
            "text": qsTr("Soft white"),
            "value": "#E8E2D6"
        },
        {
            "text": qsTr("Amber"),
            "value": "#F0A02A"
        },
        {
            "text": qsTr("Yellow"),
            "value": "#F2E14C"
        },
        {
            "text": qsTr("Cyan"),
            "value": "#7FD8E8"
        },
        {
            "text": qsTr("Grey"),
            "value": "#A0A0A0"
        }
    ]

    function indexOfValue(options, value) {
        for (let i = 0; i < options.length; ++i) {
            if (options[i].value === value)
                return i;
        }
        return -1;
    }

    // ── Current values ─────────────────────────────────────────────────────
    // Read-only mirrors of what is stored. Every control binds to one of these,
    // so arriving on the page shows the truth and never writes it.
    readonly property string currentAccent: page.prefsAvailable ? page.prefs.themeAccent : "projection"
    readonly property int storedBitrate: page.prefsAvailable ? page.prefs.maxBitrateKbps : 0
    readonly property string storedPlaybackMode: page.prefsAvailable ? page.prefs.playbackMode : "auto"

    // Subtitle appearance is edited against a draft (ARCHITECTURE.md): dragging a
    // slider repaints the preview, and only letting go writes the preference.
    // A drag that wrote on every motion event would hammer QSettings and, worse,
    // restyle a running playback dozens of times per second.
    property int draftSubtitleScale: -1
    property int draftSubtitleBackground: -1
    property int draftSubtitlePosition: -1

    readonly property int subtitleScaleValue: page.draftSubtitleScale >= 0 ? page.draftSubtitleScale
                                            : page.prefsAvailable ? page.prefs.subtitleScale : 100
    readonly property int subtitleBackgroundValue: page.draftSubtitleBackground >= 0 ? page.draftSubtitleBackground
                                                 : page.prefsAvailable ? page.prefs.subtitleBackground : 0
    readonly property int subtitlePositionValue: page.draftSubtitlePosition >= 0 ? page.draftSubtitlePosition
                                               : page.prefsAvailable ? page.prefs.subtitlePosition : 100
    readonly property string subtitleColorValue: page.prefsAvailable ? page.prefs.subtitleColor : "#FFFFFF"

    // ── Input remapping (ARCHITECTURE.md) ─────────────────────────────────────
    // Grouped off `Input.actions` rather than actionsForCategory(): the property
    // carries a change notification, so a rebind repaints the table instead of
    // leaving a stale key on screen.
    readonly property var bindingGroups: {
        const list = Input.actions;
        const order = [];
        const byCategory = ({});
        for (let i = 0; i < list.length; ++i) {
            const action = list[i];
            const category = String(action.category);
            if (byCategory[category] === undefined) {
                byCategory[category] = [];
                order.push(category);
            }
            byCategory[category].push(action);
        }
        const groups = [];
        for (let g = 0; g < order.length; ++g)
            groups.push({
                "category": order[g],
                "actions": byCategory[order[g]]
            });
        return groups;
    }

    property string captureActionId: ""

    function startCapture(actionId, actionName, sequence) {
        page.captureActionId = actionId;
        captureSheet.actionName = actionName;
        captureSheet.currentSequence = sequence;
        captureSheet.message = "";
        captureSheet.messageIsError = false;
        captureSheet.open();
    }

    // Right from the rail should land ON something, not merely inside the
    // content pane: focusing a FocusScope that has no focused child shows no
    // ring at all, which reads as the keypress having done nothing. The guard
    // keeps a wrap in the focus chain from throwing focus out of the page.
    function isInsideContent(item) {
        let node = item;
        while (node) {
            if (node === contentColumn)
                return true;
            node = node.parent;
        }
        return false;
    }

    function focusContent() {
        contentColumn.forceActiveFocus(Qt.OtherFocusReason);
        const next = contentColumn.nextItemInFocusChain(true);
        if (next && page.isInsideContent(next))
            next.forceActiveFocus(Qt.OtherFocusReason);
    }

    function endCapture() {
        page.captureActionId = "";
        captureSheet.visible = false;
        // Focus went to the sheet's key sink; hand it back to the content pane,
        // which restores whichever chip started the capture.
        contentColumn.forceActiveFocus(Qt.OtherFocusReason);
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.ground
    }

    PageHeader {
        id: header

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: Theme.pageMarginValue
        anchors.leftMargin: Theme.pageMarginValue
        anchors.rightMargin: Theme.pageMarginValue
        title: qsTr("Settings")
        subtitle: Session.username.length > 0 ? qsTr("Signed in as %1").arg(Session.username) : qsTr("Not signed in")
    }

    // ── Section rail ───────────────────────────────────────────────────────
    // A ListView rather than a Column of chips so arrow keys, wrapping rules
    // and the current-item concept all come from one place. Moving the
    // highlight *is* the selection here: a settings rail that needed Return to
    // commit would make every section two keystrokes away instead of one.
    ListView {
        id: sectionList

        anchors.top: header.bottom
        anchors.topMargin: Theme.spacingLoose
        anchors.left: parent.left
        anchors.leftMargin: Theme.pageMarginValue
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.pageMarginValue
        width: Theme.scale(196)

        focus: true
        model: page.sections
        currentIndex: page.currentSection
        keyNavigationWraps: false
        spacing: Theme.spacingTight
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds

        onCurrentIndexChanged: {
            page.currentSection = sectionList.currentIndex;
            // A new section starts at its top; carrying the old scroll offset
            // into a shorter panel lands the user on blank space.
            contentFlick.contentY = 0;
        }

        Keys.onRightPressed: event => {
            if (!event.isAutoRepeat)
                page.focusContent();
        }

        delegate: Item {
            id: sectionEntry

            required property var modelData
            required property int index

            width: sectionList.width
            height: Theme.controlHeightLarge

            readonly property bool current: sectionList.currentIndex === sectionEntry.index
            readonly property bool hovered: sectionHover.hovered

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusChip
                color: sectionEntry.current ? Theme.surfaceRaisedColor : sectionEntry.hovered ? Theme.hoverTint : "transparent"

                Behavior on color {
                    ColorAnimation {
                        duration: Theme.animInstant
                        easing.type: Theme.easeInstant
                    }
                }
            }

            // The amber marker is the "you are here", and it is deliberately
            // the only saturated thing in the rail.
            Rectangle {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.scale(3)
                height: sectionEntry.current ? parent.height * 0.55 : 0
                radius: width / 2
                color: Theme.accentColor

                Behavior on height {
                    NumberAnimation {
                        duration: Theme.animFastMs
                        easing.type: Theme.easeStandard
                    }
                }
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingValue
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingTight
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingTight

                StrmIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: sectionEntry.modelData.icon
                    size: Theme.iconSize
                    color: sectionEntry.current ? Theme.accentColor : Theme.textTertiary
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - Theme.iconSize - parent.spacing
                    text: sectionEntry.modelData.title
                    color: sectionEntry.current ? Theme.textPrimaryColor : sectionEntry.hovered ? Theme.textPrimaryColor : Theme.textSecondaryColor
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontBodySize
                    font.weight: sectionEntry.current ? Font.DemiBold : Font.Normal
                    elide: Text.ElideRight
                }
            }

            FocusRing {
                active: sectionEntry.current && sectionList.activeFocus
                radius: Theme.radiusChip
            }

            HoverHandler {
                id: sectionHover
                cursorShape: Qt.PointingHandCursor
                // No forceActiveFocus(): hovering the rail must not switch the
                // section out from under a keyboard user.
            }

            TapHandler {
                onTapped: {
                    sectionList.forceActiveFocus(Qt.MouseFocusReason);
                    sectionList.currentIndex = sectionEntry.index;
                }
            }
        }
    }

    // ── Section content ────────────────────────────────────────────────────
    Flickable {
        id: contentFlick

        anchors.top: sectionList.top
        anchors.left: sectionList.right
        anchors.leftMargin: Theme.spacingLoose
        anchors.right: parent.right
        anchors.rightMargin: Theme.pageMarginValue
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.pageMarginValue

        contentWidth: width
        contentHeight: contentColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: StrmScrollBar {}

        FocusScope {
            id: contentColumn

            width: contentFlick.width
            implicitHeight: stack.implicitHeight

            // Left goes back to the rail — the mirror of the rail's Right, and
            // the D-pad gesture a gamepad user will try first. It only fires
            // when the focused control declines the key, so a slider still
            // gets its own Left.
            Keys.onLeftPressed: event => {
                if (event.isAutoRepeat)
                    return;
                sectionList.forceActiveFocus(Qt.OtherFocusReason);
                event.accepted = true;
            }

            Column {
                id: stack

                // Capped rather than full-bleed: on a 1600 px window an
                // uncapped row puts the label at the far left and its control
                // at the far right, and the eye loses the connection between
                // the two. A measure this wide already fits the longest hint.
                width: Math.min(parent.width, Theme.scale(940))
                spacing: Theme.spacingLoose

                // ── Server ─────────────────────────────────────────────────
                StrmPanel {
                    width: parent.width
                    visible: page.currentKey === "server"
                    title: qsTr("Server")
                    subtitle: qsTr("The connection this client is signed in to.")

                    SettingsSections.InfoRow {
                        width: parent.width
                        label: qsTr("Server URL")
                        value: String(Session.serverUrl)
                        mono: true
                    }

                    SettingsSections.InfoRow {
                        width: parent.width
                        label: qsTr("Signed in as")
                        value: Session.username
                    }

                    // Server version: SessionController does not surface one
                    // yet (EmbyClient::publicSystemInfo() has it, nothing keeps
                    // it). The row appears the moment a `Session.serverVersion`
                    // property exists; until then it stays out of the way
                    // rather than showing an empty field.
                    SettingsSections.InfoRow {
                        width: parent.width
                        label: qsTr("Server version")
                        value: page.serverVersion
                        mono: true
                        visible: page.serverVersion.length > 0
                    }

                    Item {
                        width: 1
                        height: Theme.spacingTight
                    }

                    Row {
                        spacing: Theme.spacingTight

                        StrmButton {
                            text: qsTr("Switch user")
                            iconName: "user"
                            onClicked: Session.switchUser()
                        }

                        StrmButton {
                            text: qsTr("Sign out")
                            iconName: "logout"
                            destructive: true
                            onClicked: Session.logout()
                        }
                    }
                }

                // ── Appearance ─────────────────────────────────────────────
                StrmPanel {
                    width: parent.width
                    visible: page.currentKey === "appearance"
                    title: qsTr("Appearance")
                    subtitle: qsTr("One design at two sizes: the desk and the couch.")

                    SettingsSections.SettingRow {
                        width: parent.width
                        label: qsTr("Accent")
                        hint: page.prefsAvailable ? qsTr("Focus, progress and active state. Applies everywhere, immediately.") : page.unavailableHint

                        StrmSelect {
                            enabled: page.prefsAvailable
                            width: Theme.scale(220)
                            model: page.accentOptions
                            currentIndex: page.indexOfValue(page.accentOptions, page.currentAccent)
                            onActivated: index => {
                                if (page.prefsAvailable)
                                    page.prefs.themeAccent = page.accentOptions[index].value;
                            }
                        }
                    }

                    SettingsSections.SettingRow {
                        width: parent.width
                        label: qsTr("Density")
                        hint: qsTr("Compact for a desk, TV for a couch. Applies immediately.")

                        StrmSelect {
                            width: Theme.scale(220)
                            model: page.densityOptions
                            currentIndex: page.indexOfValue(page.densityOptions, Theme.densityMode)
                            onActivated: index => {
                                const value = page.densityOptions[index].value;
                                // Theme.densityMode is writable, so the change
                                // is live; Settings is what makes it survive a
                                // restart.
                                Theme.densityMode = value;
                                if (page.prefsAvailable)
                                    page.prefs.densityMode = value;
                            }
                        }
                    }

                    // Backdrop art (ARCHITECTURE.md) lands here — an on/off and an
                    // opacity — as soon as strmqt::Settings carries the two
                    // preferences behind it. Faking it with a QML-only property
                    // would give the user a switch that forgets itself on the
                    // next launch, which is worse than not having one.
                    //
                    // Home layout (F2) and per-library view modes (E2) are the
                    // other two things this panel is waiting on.
                }

                // ── Playback ───────────────────────────────────────────────
                StrmPanel {
                    width: parent.width
                    visible: page.currentKey === "playback"
                    title: qsTr("Playback")

                    SettingsSections.SettingRow {
                        width: parent.width
                        label: qsTr("Playback engine")
                        hint: qsTr("Restart to apply.")

                        StrmSelect {
                            width: Theme.scale(220)
                            model: page.engineOptions
                            currentIndex: page.indexOfValue(page.engineOptions, Session.playbackEngine)
                            onActivated: index => {
                                Session.playbackEngine = page.engineOptions[index].value;
                            }
                        }
                    }

                    SettingsSections.SettingRow {
                        width: parent.width
                        label: qsTr("Volume")
                        hint: qsTr("Remembered across sessions. Above 100% is software amplification.")

                        StrmIconButton {
                            iconName: PlayerCtl.muted ? "volume-mute" : PlayerCtl.volume < 50 ? "volume-low" : "volume-high"
                            tooltip: PlayerCtl.muted ? qsTr("Unmute") : qsTr("Mute")
                            onClicked: PlayerCtl.toggleMute()
                        }

                        StrmSlider {
                            width: Theme.scale(220)
                            from: 0
                            to: PlayerCtl.maxVolume
                            value: PlayerCtl.volume
                            stepSize: 5
                            showKnobOnHoverOnly: false
                            // Volume is not a seek: applying every intermediate
                            // value is exactly what the user is asking for, so
                            // `moved` commits as well as `committed`.
                            onMoved: v => PlayerCtl.setVolume(Math.round(v))
                            onCommitted: v => PlayerCtl.setVolume(Math.round(v))
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.scale(52)
                            text: PlayerCtl.muted ? qsTr("muted") : qsTr("%1%").arg(PlayerCtl.volume)
                            color: Theme.textSecondaryColor
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fontSmall
                            horizontalAlignment: Text.AlignRight
                        }
                    }

                    // Later milestones land here:
                    //   · Playback speed, audio delay, aspect override (D13)
                }

                // ── Continue watching ──────────────────────────────────────
                StrmPanel {
                    width: parent.width
                    visible: page.currentKey === "playback"
                    title: qsTr("Episodes")

                    SettingsSections.SettingRow {
                        width: parent.width
                        label: qsTr("Play the next episode automatically")
                        hint: page.prefsAvailable
                              ? qsTr("When an episode ends, continue with the next one. A queue or a shuffle takes precedence: those play to their own end.")
                              : page.unavailableHint

                        StrmSwitch {
                            enabled: page.prefsAvailable
                            checked: page.prefsAvailable && Prefs.autoPlayNextEpisode
                            // StrmSwitch does not self-flip: `checked` is owner-controlled,
                            // so assigning it back would write the unchanged value.
                            onToggled: Prefs.autoPlayNextEpisode = !Prefs.autoPlayNextEpisode
                        }
                    }
                }

                // ── Quality (ARCHITECTURE.md) ────────────────────────────────
                StrmPanel {
                    width: parent.width
                    visible: page.currentKey === "playback"
                    title: qsTr("Quality")
                    subtitle: qsTr("What this client asks the server for.")

                    SettingsSections.SettingRow {
                        width: parent.width
                        label: qsTr("Bitrate cap")
                        hint: page.prefsAvailable ? qsTr("A cap makes the server transcode anything above it. Auto asks for the original.") : page.unavailableHint

                        StrmSelect {
                            enabled: page.prefsAvailable
                            width: Theme.scale(220)
                            model: page.bitrateOptions
                            currentIndex: page.indexOfValue(page.bitrateOptions, page.storedBitrate)
                            placeholder: qsTr("%1 kbps").arg(page.storedBitrate)
                            onActivated: index => {
                                if (page.prefsAvailable)
                                    page.prefs.maxBitrateKbps = page.bitrateOptions[index].value;
                            }
                        }
                    }

                    SettingsSections.SettingRow {
                        width: parent.width
                        label: qsTr("Playback mode")
                        hint: page.prefsAvailable ? qsTr("Direct play only refuses anything this machine would have to transcode. Always transcode is the escape hatch for a source that will not play.") : page.unavailableHint

                        StrmSelect {
                            enabled: page.prefsAvailable
                            width: Theme.scale(220)
                            model: page.playbackModeOptions
                            currentIndex: page.indexOfValue(page.playbackModeOptions, page.storedPlaybackMode)
                            onActivated: index => {
                                if (page.prefsAvailable)
                                    page.prefs.playbackMode = page.playbackModeOptions[index].value;
                            }
                        }
                    }

                    // The single most confusing thing about these two controls:
                    // they ride along in the DeviceProfile sent with
                    // PlaybackInfo, so nothing about them can reach a stream
                    // that is already running.
                    SettingsSections.SectionNote {
                        width: parent.width
                        iconName: "info"
                        tone: Theme.warningColor
                        text: qsTr("Both settings travel with the device profile when playback starts, so they apply to the next thing you play — not to anything playing now.")
                    }
                }

                // ── Subtitles (ARCHITECTURE.md) ──────────────────────────────
                StrmPanel {
                    width: parent.width
                    visible: page.currentKey === "subtitles"
                    title: qsTr("Subtitles")
                    subtitle: qsTr("How subtitles are drawn. Changes apply to a running playback immediately.")

                    SettingsSections.SubtitlePreview {
                        width: parent.width
                        subtitleScale: page.subtitleScaleValue
                        subtitleColor: page.subtitleColorValue
                        backgroundOpacity: page.subtitleBackgroundValue
                        verticalPosition: page.subtitlePositionValue
                    }

                    Item {
                        width: 1
                        height: Theme.spacingTight
                    }

                    SettingsSections.SettingRow {
                        width: parent.width
                        label: qsTr("Size")
                        hint: page.prefsAvailable ? qsTr("Relative to the size the file or the server asks for.") : page.unavailableHint

                        StrmSlider {
                            enabled: page.prefsAvailable
                            width: Theme.scale(220)
                            from: 50
                            to: 300
                            stepSize: 5
                            showKnobOnHoverOnly: false
                            value: page.subtitleScaleValue
                            // Dragging repaints the preview; letting go is what
                            // writes the preference.
                            onMoved: v => page.draftSubtitleScale = Math.round(v)
                            onCommitted: v => {
                                if (page.prefsAvailable)
                                    page.prefs.subtitleScale = Math.round(v);
                                page.draftSubtitleScale = -1;
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.scale(64)
                            text: qsTr("%1%").arg(page.subtitleScaleValue)
                            color: Theme.textSecondaryColor
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fontSmall
                            horizontalAlignment: Text.AlignRight
                        }
                    }

                    SettingsSections.SettingRow {
                        width: parent.width
                        label: qsTr("Colour")
                        hint: page.prefsAvailable ? qsTr("White is what most releases assume; the rest are for when it disappears into the picture.") : page.unavailableHint

                        Row {
                            id: swatchRow

                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.spacingTight

                            Repeater {
                                model: page.subtitleColors

                                delegate: SettingsSections.ColorSwatch {
                                    required property var modelData

                                    enabled: page.prefsAvailable
                                    swatchColor: modelData.value
                                    label: modelData.text
                                    selected: page.subtitleColorValue.toUpperCase() === String(modelData.value).toUpperCase()
                                    onPicked: {
                                        if (page.prefsAvailable)
                                            page.prefs.subtitleColor = modelData.value;
                                    }
                                }
                            }
                        }
                    }

                    SettingsSections.SettingRow {
                        width: parent.width
                        label: qsTr("Background")
                        hint: page.prefsAvailable ? qsTr("Zero is an outline only; past halfway the outline gives way to a solid band.") : page.unavailableHint

                        StrmSlider {
                            enabled: page.prefsAvailable
                            width: Theme.scale(220)
                            from: 0
                            to: 100
                            stepSize: 5
                            showKnobOnHoverOnly: false
                            value: page.subtitleBackgroundValue
                            onMoved: v => page.draftSubtitleBackground = Math.round(v)
                            onCommitted: v => {
                                if (page.prefsAvailable)
                                    page.prefs.subtitleBackground = Math.round(v);
                                page.draftSubtitleBackground = -1;
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.scale(64)
                            text: qsTr("%1%").arg(page.subtitleBackgroundValue)
                            color: Theme.textSecondaryColor
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fontSmall
                            horizontalAlignment: Text.AlignRight
                        }
                    }

                    SettingsSections.SettingRow {
                        width: parent.width
                        label: qsTr("Vertical position")
                        hint: page.prefsAvailable ? qsTr("100 is the bottom of the picture; lower raises the text. Above 100 pushes it off the bottom edge, which is only useful on a screen with a letterbox to spare.") : page.unavailableHint

                        StrmSlider {
                            enabled: page.prefsAvailable
                            width: Theme.scale(220)
                            from: 0
                            to: 150
                            stepSize: 5
                            showKnobOnHoverOnly: false
                            value: page.subtitlePositionValue
                            onMoved: v => page.draftSubtitlePosition = Math.round(v)
                            onCommitted: v => {
                                if (page.prefsAvailable)
                                    page.prefs.subtitlePosition = Math.round(v);
                                page.draftSubtitlePosition = -1;
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.scale(64)
                            text: String(page.subtitlePositionValue)
                            color: Theme.textSecondaryColor
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fontSmall
                            horizontalAlignment: Text.AlignRight
                        }
                    }

                    Item {
                        width: 1
                        height: Theme.spacingTight
                    }

                    StrmButton {
                        enabled: page.prefsAvailable
                        text: qsTr("Restore subtitle defaults")
                        iconName: "refresh"
                        onClicked: {
                            if (!page.prefsAvailable)
                                return;
                            page.prefs.subtitleScale = 100;
                            page.prefs.subtitleColor = "#FFFFFF";
                            page.prefs.subtitleBackground = 0;
                            page.prefs.subtitlePosition = 100;
                            page.draftSubtitleScale = -1;
                            page.draftSubtitleBackground = -1;
                            page.draftSubtitlePosition = -1;
                        }
                    }
                }

                // ── Live updates (ARCHITECTURE.md) ─────────────────────────
                StrmPanel {
                    width: parent.width
                    visible: page.currentKey === "live"
                    title: qsTr("Live updates")
                    subtitle: qsTr("Whether the app follows what the server is doing, or waits to be asked.")

                    SettingsSections.SettingRow {
                        width: parent.width
                        label: qsTr("Follow the server")
                        hint: qsTr("Off means the library only changes when you refresh it.")

                        StrmSwitch {
                            checked: LiveCtl.enabled
                            // `checked` is controlled: the switch renders what
                            // the service reports and asks for a change, so the
                            // two can never disagree.
                            onToggled: LiveCtl.enabled = !LiveCtl.enabled
                        }
                    }

                    SettingsSections.SettingRow {
                        width: parent.width
                        label: qsTr("Poll every")
                        hint: qsTr("Only used when the event socket cannot connect. Polling pauses during playback.")

                        StrmSelect {
                            enabled: LiveCtl.enabled
                            width: Theme.scale(220)
                            model: page.pollOptions
                            currentIndex: page.indexOfValue(page.pollOptions, LiveCtl.pollIntervalSeconds)
                            placeholder: qsTr("%1 s").arg(LiveCtl.pollIntervalSeconds)
                            onActivated: index => {
                                LiveCtl.pollIntervalSeconds = page.pollOptions[index].value;
                            }
                        }
                    }

                    SettingsSections.InfoRow {
                        width: parent.width
                        label: qsTr("Transport")
                        value: LiveCtl.transport
                        mono: true
                    }

                    SettingsSections.InfoRow {
                        width: parent.width
                        label: qsTr("Socket")
                        value: LiveCtl.connected ? qsTr("connected") : qsTr("not connected")
                        mono: true
                    }

                    Item {
                        width: 1
                        height: Theme.spacingTight
                    }

                    StrmButton {
                        text: qsTr("Refresh now")
                        iconName: "refresh"
                        onClicked: LiveCtl.refreshNow()
                    }
                }

                // ── Input (ARCHITECTURE.md) ───────────────────────────────────
                StrmPanel {
                    width: parent.width
                    visible: page.currentKey === "input"
                    title: qsTr("Input")
                    subtitle: qsTr("Every binding, from one source of truth. Click a key to change it.")

                    SettingsSections.SectionNote {
                        width: parent.width
                        text: qsTr("Two actions may share a key when they can never be live at once — Space selects while browsing and pauses in the player. A key already taken in an overlapping context is refused, and the old binding stands.")
                    }

                    Item {
                        width: 1
                        height: Theme.spacingTight
                    }

                    Repeater {
                        model: page.bindingGroups

                        delegate: Column {
                            id: categoryBlock

                            required property var modelData

                            // Repeater delegates are parented to the panel's
                            // body column, so `parent` is that column.
                            width: parent.width
                            spacing: Theme.scale(2)
                            bottomPadding: Theme.spacingValue

                            Text {
                                text: categoryBlock.modelData.category
                                color: Theme.textTertiary
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontCaption
                                font.capitalization: Font.AllUppercase
                                font.letterSpacing: Theme.trackLabel * Theme.fontCaption
                                bottomPadding: Theme.spacingTight
                            }

                            Repeater {
                                model: categoryBlock.modelData.actions

                                delegate: SettingsSections.BindingRow {
                                    id: bindingRow

                                    required property var modelData

                                    width: categoryBlock.width
                                    actionName: bindingRow.modelData.name
                                    sequence: String(bindingRow.modelData.sequence)
                                    gamepad: String(bindingRow.modelData.gamepad)
                                    custom: bindingRow.modelData.custom === true
                                    capturing: page.captureActionId === bindingRow.modelData.actionId

                                    onEditRequested: page.startCapture(bindingRow.modelData.actionId, bindingRow.modelData.name, String(bindingRow.modelData.sequence))
                                    onResetRequested: Input.resetBinding(bindingRow.modelData.actionId)
                                }
                            }
                        }
                    }

                    StrmButton {
                        text: qsTr("Restore all defaults")
                        iconName: "refresh"
                        onClicked: Input.resetAll()
                    }
                }

                // ── About ──────────────────────────────────────────────────
                StrmPanel {
                    width: parent.width
                    visible: page.currentKey === "about"
                    title: qsTr("About")

                    SettingsSections.InfoRow {
                        width: parent.width
                        label: qsTr("StrmQt")
                        value: Qt.application.version
                        mono: true
                    }

                    // Qt runtime version: QML has no API for it and no
                    // controller carries QT_VERSION_STR today. The row appears
                    // once one does (a `qtVersion` on the application object,
                    // exposed like the others in main.cpp).
                    SettingsSections.InfoRow {
                        width: parent.width
                        label: qsTr("Qt")
                        value: page.qtVersion
                        mono: true
                        visible: page.qtVersion.length > 0
                    }

                    SettingsSections.InfoRow {
                        width: parent.width
                        label: qsTr("Playback engine")
                        value: Session.playbackEngine
                        mono: true
                    }

                    SettingsSections.InfoRow {
                        width: parent.width
                        label: qsTr("License")
                        value: qsTr("GPL-3.0-or-later")
                    }

                    Text {
                        width: parent.width
                        text: qsTr("A native Qt client for Emby. No .NET, no SDKs — QtNetwork against the REST API and libmpv for playback.")
                        color: Theme.textTertiary
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSmall
                        lineHeight: Theme.lineNormal
                        lineHeightMode: Text.ProportionalHeight
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    // ── Rebinding (ARCHITECTURE.md) ───────────────────────────────────────────
    // The sheet is above everything, including the rail, because while it is up
    // every key belongs to it.
    SettingsSections.KeyCaptureSheet {
        id: captureSheet

        anchors.fill: parent

        onCaptured: (key, modifiers) => {
            const sequence = Input.sequenceForKey(key, modifiers);
            if (sequence.length === 0) {
                captureSheet.messageIsError = true;
                captureSheet.message = qsTr("That key cannot be used as a shortcut.");
                return;
            }
            // setBinding() reports a clash through bindingConflict, handled
            // below; a true return means it took.
            if (Input.setBinding(page.captureActionId, sequence))
                page.endCapture();
        }

        onCleared: {
            Input.resetBinding(page.captureActionId);
            page.endCapture();
        }

        onDismissed: page.endCapture()
    }

    Connections {
        target: Input

        function onBindingConflict(actionId: string, sequence: string, conflictingActionId: string): void {
            if (actionId !== page.captureActionId)
                return;
            captureSheet.messageIsError = true;
            captureSheet.message = conflictingActionId.length > 0
                ? qsTr("%1 is already bound to “%2”. Pick another key.").arg(sequence).arg(Input.displayName(conflictingActionId))
                : qsTr("That key cannot be used as a shortcut.");
        }
    }

    // Filled in when a controller carries them; see the comments at each row.
    readonly property string serverVersion: ""
    readonly property string qtVersion: ""
}
