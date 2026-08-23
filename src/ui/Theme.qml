pragma Singleton
import QtQuick

// StrmQt design tokens — "Projection Booth" (ARCHITECTURE.md).
//
// Everything visual comes from here: no page or control may hardcode a colour,
// a size, or a duration. Names are ROLES, not values, so re-theming is a change
// to this file alone.
//
// The legacy Breeze-era names (background, fontSizeBody, cardRadius, …) existed
// as aliases through the M9/M10 migration and were deleted once no page
// referenced them. Their removal is what proves the migration actually finished:
// a page still on an old name is now a build error, not a judgement call.
QtObject {
    id: theme

    // ── Fonts ──────────────────────────────────────────────────────────────
    // Bundled into the binary's resources (assets/fonts → qrc:/fonts) so the
    // Flatpak and AppImage render identically to a native build.
    readonly property FontLoader _display: FontLoader {
        source: "qrc:/fonts/Archivo[wdth,wght].ttf"
    }
    readonly property FontLoader _body: FontLoader {
        source: "qrc:/fonts/PublicSans[wght].ttf"
    }
    readonly property FontLoader _mono: FontLoader {
        source: "qrc:/fonts/IBMPlexMono-Regular.ttf"
    }
    readonly property FontLoader _monoMedium: FontLoader {
        source: "qrc:/fonts/IBMPlexMono-Medium.ttf"
    }
    readonly property FontLoader _monoSemi: FontLoader {
        source: "qrc:/fonts/IBMPlexMono-SemiBold.ttf"
    }

    // Titles, headings, marquee text. Wide and confident at 10 feet.
    readonly property string fontDisplay: _display.status === FontLoader.Ready
                                          ? _display.name : "sans-serif"
    // Body, metadata, controls. Open apertures, honest tabular numerals.
    readonly property string fontBody: _body.status === FontLoader.Ready
                                       ? _body.name : "sans-serif"
    // Timecode, codec/bitrate chips, hwdec readouts, stats overlay.
    readonly property string fontMono: _mono.status === FontLoader.Ready
                                       ? _mono.name : "monospace"

    // ── Palette ────────────────────────────────────────────────────────────
    // Warm near-black, not blue-black: grain and poster art both read warmer,
    // and amber stops looking like an error state against a cold ground.
    readonly property color ground: "#0C0B0A"          // page background
    readonly property color surfaceColor: "#141210"    // cards, rails, inactive chrome
    readonly property color surfaceRaisedColor: "#1D1A17" // hovered card, OSD panels
    readonly property color surfaceOverlay: "#262220"  // menus, dialogs
    readonly property color hairline: "#2E2A26"        // 1 px separators, card edges

    // ── Accent ─────────────────────────────────────────────────────────────
    // Projection-booth amber is the default because it sits outside the hue
    // range of most poster art, so a focus ring is never lost against the
    // artwork it frames (ARCHITECTURE.md). The other three exist so the app can
    // read as first-party Emby/Jellyfin, or match the desktop, on request.
    //
    // Read from Settings via the `Prefs` context property, guarded because the
    // singleton is also instantiated by tooling (qmllint, the `qml` runtime)
    // where no context property exists — there it falls back to the default
    // rather than throwing a ReferenceError and taking every colour with it.
    property string accentName: typeof Prefs !== "undefined" ? Prefs.themeAccent : "projection"

    readonly property var _accents: ({
        "projection": { accent: "#F0A02A", muted: "#7A5416", text: "#0C0B0A" },
        "emby":       { accent: "#52B54B", muted: "#2C5C29", text: "#0C0B0A" },
        "jellyfin":   { accent: "#AA5CC3", muted: "#553063", text: "#0C0B0A" },
        "breeze":     { accent: "#3DAEE9", muted: "#1E5872", text: "#0C0B0A" }
    })
    readonly property var _accent: _accents[accentName] !== undefined ? _accents[accentName]
                                                                      : _accents["projection"]

    readonly property color accentColor: theme._accent.accent  // focus, progress, active
    readonly property color accentMuted: theme._accent.muted   // progress track, disabled
    readonly property color accentText: theme._accent.text     // text/icons on accent fills

    readonly property color textPrimaryColor: "#F5F1EA"
    readonly property color textSecondaryColor: "#A29A8E"
    readonly property color textTertiary: "#6E675E"
    readonly property color textDisabled: "#4A453F"

    // Semantic, deliberately separate from the accent.
    readonly property color positive: "#6FBF73"        // watched, direct play
    readonly property color warningColor: "#E0A33E"    // transcoding, degraded
    readonly property color negative: "#E05C5C"        // errors, record

    readonly property color scrimColor: Qt.rgba(0.047, 0.043, 0.039, 0.86)
    readonly property color veil: Qt.rgba(0.047, 0.043, 0.039, 0.45)
    readonly property color hoverTint: Qt.rgba(0.96, 0.945, 0.918, 0.10)
    readonly property color pressTint: Qt.rgba(0.96, 0.945, 0.918, 0.16)

    // ── Density ────────────────────────────────────────────────────────────
    // One design at two sizes: the desk and the couch get the same tokens
    // scaled, instead of one audience being served and the other apologised to.
    // Settings drives this; InputMap flips it to "tv" when a gamepad wakes up.
    // Persisted; SettingsPage writes it live and Settings makes it survive a
    // restart. Same Prefs guard as the accent above.
    property string densityMode: typeof Prefs !== "undefined" ? Prefs.densityMode
                                                              : "comfortable"
    readonly property real density: densityMode === "compact" ? 0.9
                                  : densityMode === "tv" ? 1.15 : 1.0

    function scale(value) { return Math.round(value * theme.density) }

    // ── Type ramp ──────────────────────────────────────────────────────────
    readonly property int fontCaption: scale(12)
    readonly property int fontSmall: scale(14)
    readonly property int fontBodySize: scale(16)
    readonly property int fontBodyLarge: scale(18)
    readonly property int fontTitle: scale(22)
    readonly property int fontHeading: scale(30)
    readonly property int fontDisplaySize: scale(44)
    readonly property int fontHero: scale(64)

    readonly property real lineTight: 1.1
    readonly property real lineNormal: 1.45
    readonly property real lineLoose: 1.62
    readonly property real trackLabel: 0.13 // uppercase mono label letter-spacing (em)

    // ── Radii ──────────────────────────────────────────────────────────────
    readonly property int radiusChip: 4
    readonly property int radiusCardValue: 8
    readonly property int radiusPanel: 12
    readonly property int radiusPill: 999

    // ── Elevation ──────────────────────────────────────────────────────────
    // Shadow parameters for QtQuick.Effects MultiEffect. Surface + shadow are a
    // pair; components must not invent a "slightly lighter" colour instead.
    readonly property var elevation1: ({ blur: 0.18, y: 1, opacity: 0.50 })
    readonly property var elevation2: ({ blur: 0.42, y: 8, opacity: 0.62 })
    readonly property var elevation3: ({ blur: 0.62, y: 14, opacity: 0.70 })
    readonly property var elevation4: ({ blur: 0.85, y: 22, opacity: 0.80 })

    // ── Motion ─────────────────────────────────────────────────────────────
    // The rule that matters more than the numbers: hover is `instant`, focus is
    // `fast`. Hover must track the cursor with no perceived lag; focus is a
    // deliberate act and can afford to glide (ARCHITECTURE.md).
    readonly property int animInstant: 90
    readonly property int animFastMs: 160
    readonly property int animNormalMs: 240
    readonly property int animSlow: 420
    readonly property int animAmbient: 1200

    readonly property int easeInstant: Easing.OutQuad
    readonly property int easeStandard: Easing.OutCubic
    readonly property int easeEmphasis: Easing.OutQuint
    readonly property int easeAmbient: Easing.InOutSine

    // ── Interaction geometry ───────────────────────────────────────────────
    readonly property real hoverScale: 1.035
    readonly property real focusScale: 1.05
    readonly property real pressScale: 0.97
    readonly property int focusRingWidth: 3
    readonly property int controlHeight: scale(38)
    readonly property int controlHeightLarge: scale(46)
    readonly property int iconSize: scale(19)
    readonly property int touchTarget: scale(44)

    // ── Layout metrics ─────────────────────────────────────────────────────
    readonly property int posterWidthValue: scale(178)
    readonly property int posterHeightValue: scale(267)      // 2:3
    readonly property int stillWidth: scale(320)
    readonly property int stillHeight: scale(180)            // 16:9
    readonly property int libraryTileWidthValue: scale(322)
    readonly property int libraryTileHeightValue: scale(181) // 16:9
    readonly property int spacingTight: scale(8)
    readonly property int spacingValue: scale(16)
    readonly property int spacingLoose: scale(24)
    readonly property int pageMarginValue: scale(48)
    readonly property int railGap: scale(32)
    readonly property int topBarHeight: scale(52)
}
