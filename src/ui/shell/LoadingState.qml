pragma ComponentBehavior: Bound
import QtQuick
import StrmQt

// LoadingState — the shape of a page before its data arrives (ARCHITECTURE.md).
//
// Pages went blank and then popped: nothing, nothing, nothing, everything at
// once. A skeleton in the *shape of the answer* is what removes that flinch, so
// this composes StrmSkeleton into the four layouts the app actually has, rather
// than offering a generic spinner:
//
//   rails    Home — a heading and a row of posters, three times over
//   grid     Library / Search results — a poster grid that fills the width
//   list     Series episodes, settings sections — full-width rows
//   details  Details / Series header — backdrop, poster, title, metadata
//
//   LoadingState { anchors.fill: parent; shape: "grid"; active: LibraryCtl.loading }
//
// Everything is clipped to the component's bounds and driven by one shimmer
// implementation, so the placeholder cannot drift away from the real layout's
// metrics: both read the same Theme tokens.
Item {
    id: loading

    // "rails" | "grid" | "list" | "details"
    property string shape: "rails"
    property bool active: true
    // Forwarded to every StrmSkeleton; the caller owns the reduced-motion
    // decision until Theme grows a token for it.
    property bool reduceMotion: false
    property int margins: Theme.pageMarginValue

    visible: loading.active
    clip: true

    Loader {
        anchors.fill: parent
        anchors.margins: loading.margins
        active: loading.active
        sourceComponent: loading.shape === "grid" ? gridShape
                       : loading.shape === "list" ? listShape
                       : loading.shape === "details" ? detailsShape
                       : railsShape
    }

    // ── Home: heading + poster row, repeated ───────────────────────────────
    Component {
        id: railsShape

        Column {
            spacing: Theme.railGap

            Repeater {
                model: 3

                Column {
                    spacing: Theme.spacingValue

                    StrmSkeleton {
                        width: Theme.scale(180)
                        height: Theme.fontTitle
                        radius: Theme.radiusChip
                        reduceMotion: loading.reduceMotion
                    }

                    Row {
                        spacing: Theme.spacingValue

                        Repeater {
                            model: 7

                            StrmSkeleton {
                                width: Theme.posterWidthValue
                                height: Theme.posterHeightValue
                                reduceMotion: loading.reduceMotion
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Library / Search: a poster grid that fills the width ───────────────
    Component {
        id: gridShape

        Flow {
            spacing: Theme.spacingValue

            Repeater {
                model: 24

                StrmSkeleton {
                    width: Theme.posterWidthValue
                    height: Theme.posterHeightValue
                    reduceMotion: loading.reduceMotion
                }
            }
        }
    }

    // ── Episodes / settings: full-width rows ───────────────────────────────
    Component {
        id: listShape

        Column {
            id: listColumn
            spacing: Theme.spacingValue

            Repeater {
                model: 8

                StrmSkeleton {
                    width: listColumn.width
                    height: Theme.controlHeightLarge
                    reduceMotion: loading.reduceMotion
                }
            }
        }
    }

    // ── Details / Series header ────────────────────────────────────────────
    Component {
        id: detailsShape

        Column {
            id: detailsColumn
            spacing: Theme.spacingLoose

            StrmSkeleton {
                width: detailsColumn.width
                height: Math.round(detailsColumn.width * 0.28)
                reduceMotion: loading.reduceMotion
            }

            Row {
                spacing: Theme.spacingLoose

                StrmSkeleton {
                    width: Theme.posterWidthValue
                    height: Theme.posterHeightValue
                    reduceMotion: loading.reduceMotion
                }

                Column {
                    spacing: Theme.spacingValue

                    StrmSkeleton {
                        width: Theme.scale(360)
                        height: Theme.fontHeading
                        radius: Theme.radiusChip
                        reduceMotion: loading.reduceMotion
                    }

                    Repeater {
                        model: 4

                        StrmSkeleton {
                            required property int index

                            width: Theme.scale(index % 2 === 0 ? 480 : 400)
                            height: Theme.fontBodySize
                            radius: Theme.radiusChip
                            reduceMotion: loading.reduceMotion
                        }
                    }
                }
            }
        }
    }
}
