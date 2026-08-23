import QtQuick
import StrmQt

// UpNextCard — the last-30-seconds offer (ARCHITECTURE.md).
//
// Before the queue existed, playback simply ended and the page popped. Now the
// controller counts down and this card is what the countdown looks like: the
// next item's art, what it is, how long until it starts, and the two things you
// might want to do about it.
//
// It lives OUTSIDE the OSD's fading chrome on purpose. The OSD hides after 3 s
// of idle, and a card that offered to play the next episode only while you were
// moving the mouse would be a card nobody ever saw.
//
// Cancel calls cancelUpNext(), which suppresses the auto-advance for this item
// only — it does not turn the feature off, and it does not stop playback.
//
// A FocusScope rather than a bare Item: `focus: true` on Play now must mean
// "the default button *within this card*". On a plain Item it would mean the
// enclosing scope — the player page — and the card would silently steal the
// keyboard the moment it was built.
FocusScope {
    id: card

    // Raised when the card is dismissed either way, so the OSD can restart its
    // idle timer rather than hiding out from under a click.
    signal dismissed

    readonly property bool active: PlayerCtl.upNextVisible === true
    readonly property var nextItem: {
        const item = PlayerCtl.nextItem;
        return (item !== undefined && item !== null) ? item : ({});
    }
    readonly property int secondsRemaining: {
        const value = PlayerCtl.upNextSecondsRemaining;
        return value !== undefined ? Math.max(0, Number(value)) : 0;
    }

    readonly property string nextLabel: {
        const item = card.nextItem;
        if (item.label !== undefined && String(item.label).length > 0)
            return String(item.label);
        if (item.name !== undefined && String(item.name).length > 0)
            return String(item.name);
        return qsTr("Up next");
    }
    readonly property string nextSubline: {
        const item = card.nextItem;
        return item.subtitle !== undefined ? String(item.subtitle) : "";
    }
    readonly property string nextArt: {
        const item = card.nextItem;
        if (item.posterUrl !== undefined && String(item.posterUrl).length > 0)
            return String(item.posterUrl);
        if (item.backdropUrl !== undefined && String(item.backdropUrl).length > 0)
            return String(item.backdropUrl);
        return "";
    }

    implicitWidth: Theme.scale(420)
    implicitHeight: Theme.scale(140)
    width: implicitWidth
    height: implicitHeight

    visible: card.opacity > 0.01
    enabled: card.active
    opacity: card.active ? 1 : 0

    // Fade only. The owner anchors this card, so animating `y` here would be a
    // second authority over the same coordinate and anchors would win anyway.
    Behavior on opacity {
        NumberAnimation {
            duration: Theme.animNormalMs
            easing.type: Theme.easeStandard
        }
    }

    function playNow(): void {
        card.dismissed();
        PlayerCtl.playNext();
    }

    function cancel(): void {
        card.dismissed();
        PlayerCtl.cancelUpNext();
    }

    StrmPanel {
        anchors.fill: parent
        padding: Theme.spacingValue
        elevation: 4

        Item {
            width: parent.width
            height: Theme.scale(64)

            Rectangle {
                id: art

                anchors.left: parent.left
                anchors.top: parent.top
                width: Theme.scale(44)
                height: Theme.scale(64)
                radius: Theme.radiusChip
                color: Theme.surfaceColor
                clip: true

                Image {
                    anchors.fill: parent
                    source: card.nextArt
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize.width: Theme.scale(88)
                    visible: status === Image.Ready
                }
            }

            Column {
                anchors.left: art.right
                anchors.leftMargin: Theme.spacingValue
                anchors.right: parent.right
                anchors.verticalCenter: art.verticalCenter
                spacing: Theme.scale(2)

                Text {
                    width: parent.width
                    text: qsTr("Up next")
                    color: Theme.accentColor
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontCaption
                    font.letterSpacing: Theme.trackLabel * Theme.fontCaption
                }

                Text {
                    width: parent.width
                    text: card.nextLabel
                    color: Theme.textPrimaryColor
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.fontBodyLarge
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Text {
                    id: subline

                    width: parent.width
                    visible: subline.text.length > 0
                    text: card.nextSubline
                    color: Theme.textSecondaryColor
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontCaption
                    elide: Text.ElideRight
                }
            }
        }

        Item {
            width: parent.width
            height: Theme.controlHeight

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Starts in %1 s").arg(card.secondsRemaining)
                color: Theme.textSecondaryColor
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSmall
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingTight

                StrmButton {
                    id: cancelButton

                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Cancel")
                    variant: "ghost"
                    onClicked: card.cancel()

                    KeyNavigation.right: playButton
                }

                StrmButton {
                    id: playButton

                    anchors.verticalCenter: parent.verticalCenter
                    focus: true
                    text: qsTr("Play now")
                    variant: "primary"
                    iconName: "play"
                    onClicked: card.playNow()

                    KeyNavigation.left: cancelButton
                }
            }
        }
    }
}
