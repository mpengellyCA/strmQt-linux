import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// Overlay scrollbar for grids and long lists (ARCHITECTURE.md). Attach it, don't
// lay it out:
//
//   ScrollBar.vertical: StrmScrollBar {}
//
// Thin and quiet at rest, brighter under the pointer, and it fades out when
// nothing is moving — a mouse user gets position feedback and something to
// drag, without a permanent gutter cutting into the artwork.
ScrollBar {
    id: bar

    hoverEnabled: true
    padding: Theme.scale(2)
    minimumSize: 0.06

    contentItem: Rectangle {
        implicitWidth: Theme.scale(6)
        implicitHeight: Theme.scale(6)
        radius: Math.min(width, height) / 2
        color: bar.pressed ? Theme.textPrimaryColor
             : bar.hovered ? Theme.textSecondaryColor
             : Theme.textTertiary
        opacity: (bar.policy === ScrollBar.AlwaysOn || bar.active || bar.hovered || bar.pressed)
                 ? 1.0 : 0.0

        Behavior on color {
            ColorAnimation {
                duration: Theme.animInstant
                easing.type: Theme.easeInstant
            }
        }

        Behavior on opacity {
            NumberAnimation {
                duration: Theme.animNormalMs
                easing.type: Theme.easeStandard
            }
        }
    }

    background: Rectangle {
        color: bar.hovered || bar.pressed ? Theme.surfaceColor : "transparent"
        radius: Theme.radiusPill

        Behavior on color {
            ColorAnimation {
                duration: Theme.animInstant
                easing.type: Theme.easeInstant
            }
        }
    }
}
