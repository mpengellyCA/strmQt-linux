pragma ComponentBehavior: Bound
import QtQuick
import StrmQt

// CrateDividers — the A–Z tabs down the right edge of a crate (Crate spec §5.1).
//
// One tab stop, like FilterBar's strip: 27 focusable letters would flood the
// focus chain. Up/Down move a preview cursor; only Return, Space or a click
// chooses. The chosen letter protrudes left in amber, the way a divider card
// stands proud of the records filed behind it.
//
// It stores nothing: `currentLetter` is the controller's, and a pick goes out
// as letterChosen() for the page to hand back (MusicBrowseCtl.toggleLetter).
//
// Ruling P2-R2: each letter tab below is explicitly `activeFocusOnTab: false`
// (like `StrmRail`'s hover chevrons) so 27 letters never add 27 Tab stops —
// `dividers` itself is the one stop, and its `FocusRing` is the only visible
// focus state.
FocusScope {
    id: dividers

    property var letters: []
    property string currentLetter: ""
    property int cursor: Math.max(0, dividers.letters.indexOf(dividers.currentLetter))

    signal letterChosen(string letter)

    readonly property bool hovered: hover.hovered
    readonly property real cellHeight: dividers.letters.length > 0
                                       ? Math.max(Theme.scale(12), Math.min(Theme.scale(22),
                                                  dividers.height / dividers.letters.length))
                                       : 0

    implicitWidth: Theme.scale(30)
    // `|| activeFocus` because `letters` can empty while this scope still holds
    // the keyboard (a filter or section change refills it from the controller).
    // Measured: a guard tied to a plain condition — neither `visible` nor
    // `enabled` — is the worst shape of all. Qt clears focus BEFORE an
    // `enabled` binding re-evaluates, so those are safe; it does not for a
    // plain condition, so QQuickItem refuses the write ("Cannot set
    // activeFocusOnTab to false once item is the active focus item"), keeps
    // `true`, and never re-evaluates. Unlike a hidden item — which Qt's Tab
    // traversal skips anyway — these dividers stay VISIBLE, and NavRail.qml:43
    // and :98 read `activeFocusOnTab` directly to build their navigation sets.
    activeFocusOnTab: dividers.letters.length > 0 || dividers.activeFocus

    Accessible.role: Accessible.List
    Accessible.name: qsTr("Jump to letter")
    Accessible.focusable: true
    Accessible.focused: dividers.activeFocus

    onCurrentLetterChanged: dividers.cursor = Math.max(0, dividers.letters.indexOf(dividers.currentLetter))
    // `letters` and `currentLetter` are two independent bindings a consumer
    // sets at construction (MusicBrowseCtl.letters / .letter); QML gives no
    // guarantee which fires first, so `onCurrentLetterChanged` above can run
    // once against a still-default `letters` and leave `cursor` wrong until
    // the next real change. `onCompleted` runs only once every property in
    // this object has its final constructed value, so it resyncs `cursor`
    // unconditionally.
    Component.onCompleted: dividers.cursor = Math.max(0, dividers.letters.indexOf(dividers.currentLetter))

    function choose(index): void {
        if (index < 0 || index >= dividers.letters.length)
            return;
        dividers.cursor = index;
        dividers.letterChosen(dividers.letters[index]);
    }

    Column {
        id: column

        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter

        Repeater {
            model: dividers.letters

            delegate: Item {
                id: tab

                required property int index
                required property var modelData

                readonly property bool chosen: String(tab.modelData) === dividers.currentLetter
                readonly property bool previewed: dividers.activeFocus && dividers.cursor === tab.index

                objectName: "crateDividersLetter-" + tab.modelData
                activeFocusOnTab: false
                width: dividers.width
                height: dividers.cellHeight

                Rectangle {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: parent.height - Theme.scale(2)
                    // The chosen divider stands proud of the rest.
                    width: tab.chosen ? parent.width : parent.width - Theme.scale(8)
                    radius: Theme.scale(2)
                    color: tab.chosen ? Theme.accentColor
                         : tab.previewed ? Theme.surfaceRaisedColor
                         : Theme.surfaceColor
                    border.width: tab.previewed && !tab.chosen ? 1 : 0
                    border.color: Theme.accentColor

                    Behavior on width {
                        NumberAnimation { duration: Theme.animFastMs; easing.type: Theme.easeStandard }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: String(tab.modelData)
                        color: tab.chosen ? Theme.accentText
                             : tab.previewed ? Theme.textPrimaryColor
                             : Theme.textSecondaryColor
                        font.family: Theme.fontMono
                        font.pixelSize: Math.min(Theme.crateKickerSize, Math.round(dividers.cellHeight * 0.7))
                        font.weight: tab.chosen ? Font.Bold : Font.Normal
                    }
                }

                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: {
                        dividers.forceActiveFocus(Qt.MouseFocusReason);
                        dividers.choose(tab.index);
                    }
                }
            }
        }
    }

    FocusRing {
        objectName: "crateDividersFocusRing"
        active: dividers.activeFocus
        radius: Theme.radiusChip
        inset: -Theme.scale(2)
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    Keys.onUpPressed: event => {
        dividers.cursor = Math.max(0, dividers.cursor - 1);
        event.accepted = true;
    }
    Keys.onDownPressed: event => {
        dividers.cursor = Math.min(dividers.letters.length - 1, dividers.cursor + 1);
        event.accepted = true;
    }
    Keys.onReturnPressed: event => {
        if (!event.isAutoRepeat)
            dividers.choose(dividers.cursor);
    }
    Keys.onEnterPressed: event => {
        if (!event.isAutoRepeat)
            dividers.choose(dividers.cursor);
    }
    Keys.onSpacePressed: event => {
        if (!event.isAutoRepeat)
            dividers.choose(dividers.cursor);
    }
    // A digit is also a window Shortcut (the number keys open libraries), and
    // shortcuts are matched before Keys.onPressed; claim it so a typed digit
    // keeps choosing "#" while the dividers hold focus.
    Keys.onShortcutOverride: event => {
        if (dividers.activeFocus && event.key >= Qt.Key_0 && event.key <= Qt.Key_9
                && (event.modifiers & (Qt.ControlModifier | Qt.AltModifier
                                       | Qt.MetaModifier)) === 0)
            event.accepted = true;
    }
    // A typed letter chooses directly, as FilterBar's strip does. "#" is
    // first in MusicBrowseCtl.letters, so a typed digit chooses index 0.
    Keys.onPressed: event => {
        if (event.text.length !== 1 || event.modifiers & (Qt.ControlModifier | Qt.AltModifier))
            return;
        const upper = event.text.toUpperCase();
        const index = /^[0-9]$/.test(upper) ? 0 : dividers.letters.indexOf(upper);
        if (index >= 0) {
            dividers.choose(index);
            event.accepted = true;
        }
    }
}
