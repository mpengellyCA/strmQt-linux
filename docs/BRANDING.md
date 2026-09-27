# StrmQt Brand & Visual Identity Guidelines

> **Projection Booth** — A cinematic, warm-black, couch-first design language for StrmQt.

---

## 1. Brand Essence & Story

**StrmQt** is a native, high-performance Qt 6 client for Emby media servers on Linux, built for KDE Plasma, HTPCs, and the Steam Deck. It is designed to be driven from the couch at 10 feet with a gamepad or remote, just as fluidly as from a desktop with a mouse and keyboard.

The visual identity of StrmQt is named **"Projection Booth"**:
* **Cinematic Warmth**: In contrast to cold corporate blue-blacks, StrmQt lives in a warm near-black ground (`#0C0B0A`). Film grain, vintage typography, and rich poster art glow naturally against it.
* **Incandescent Amber**: The primary brand accent is **Projection Amber** (`#F0A02A`). Inspired by vintage 35mm projector bulbs and darkroom amber lamps, it commands immediate visual authority and sits outside the hue range of most movie posters—guaranteeing focus rings never disappear against cover art.
* **Optical Precision**: The brand mark integrates a mechanical cinema aperture shutter with the letter "Q" and a forward play triangle, symbolizing streaming media playback driven by native engineering.

---

## 2. Brand Name & Verbal Identity

### Naming Conventions
| Context | Approved Spelling | Unapproved (Do Not Use) |
| :--- | :--- | :--- |
| **Official Name** | **StrmQt** | STRMQT, Strmqt, StreamQt, strm-qt |
| **CLI Binary** | `strmqt`, `strmqt-cli` | `strm-qt`, `StrmQt` |
| **Reverse-DNS App ID** | `ca.mikesdev.StrmQt` | `org.strmqt`, `com.embyqt` |

### Pronunciation & Voice
* **Pronunciation**: *"Stream-Q-T"* (phonetically /striːm kjuː tiː/).
* **Tone & Voice**:
  * **Utilitarian & Honest**: Fast, direct, zero marketing fluff.
  * **Cinematic & Respectful of Media**: The chrome yields to the artwork and audio. The player does not show off; it displays the stream with hardware precision.
  * **Linux-Native**: Proudly built on direct C APIs (`libmpv`, `libvlc`, `SDL3`, `Qt 6`, `KDE KWallet`), not electron or web wrappers.

---

## 3. Brand Mark & Logo System

The StrmQt brand mark is the **Aperture-Q**:
1. **The Iris Ring**: A 6-blade mechanical lens aperture iris representing cinema projection, optics, and photography.
2. **The Q-Tail**: An angled geometric stem extending from the lower-right blade at 45°, creating the letter **Q**.
3. **The Play Core**: An optically centered playback triangle pointing forward, representing streaming and motion pictures.

```
       ┌─────────────────────────┐
       │      Aperture Ring      │   <- Cinema Optics / Shutter
       │    ┌───────────────┐    │
       │    │   ► (Play)    │    │   <- Media Playback
       │    └───────────────┘    │
       │                    \    │   <- "Q" Kick Tail
       └─────────────────────\───┘
```

### Logo Variations & Vector Assets

All assets are located in [`assets/brand/`](../assets/brand/) as pure, standalone SVG paths:

| Asset | File | Description & Usage |
| :--- | :--- | :--- |
| **Primary Horizontal Logo** | [`strmqt-logo-horizontal.svg`](../assets/brand/strmqt-logo-horizontal.svg) | Main lockup on warm dark backgrounds. Contains symbol, "Strm" in Cream White, "Qt" in Projection Amber, and uppercase subtext. |
| **Light Horizontal Logo** | [`strmqt-logo-horizontal-light.svg`](../assets/brand/strmqt-logo-horizontal-light.svg) | Horizontal lockup for light paper, documentation, or light backgrounds. |
| **Stacked Vertical Logo** | [`strmqt-logo-stacked.svg`](../assets/brand/strmqt-logo-stacked.svg) | Centered symbol above wordmark. Ideal for splash screens and square formats. |
| **Standalone Brand Symbol** | [`strmqt-symbol.svg`](../assets/brand/strmqt-symbol.svg) | Standalone Aperture-Q symbol with rich amber gradient and warm glow. |
| **Monochrome White** | [`strmqt-symbol-monochrome-white.svg`](../assets/brand/strmqt-symbol-monochrome-white.svg) | Single-color white symbol for watermarks, video overlays, and engraving. |
| **Monochrome Dark** | [`strmqt-symbol-monochrome-dark.svg`](../assets/brand/strmqt-symbol-monochrome-dark.svg) | Single-color dark symbol for light printed material. |
| **Showcase Badge** | [`strmqt-badge.svg`](../assets/brand/strmqt-badge.svg) | Framed squircle badge with subtle hairline border. Used in GitHub README, store listings, and social cards. |

### Clear Space & Minimum Sizing
* **Clear Space**: Maintain a margin around the logo equal to at least the height of the letter **Q** ($1X$). No typography, borders, or busy graphical elements may intrude into this zone.
* **Minimum Digital Sizes**:
  * Standalone Symbol: Minimum **24 px** height.
  * Horizontal Lockup: Minimum **120 px** width (to preserve subtext legibility).
  * Print Reproduction: Minimum **10 mm** height.

---

## 4. Color Palette

StrmQt's color hierarchy is defined in [`src/ui/Theme.qml`](../src/ui/Theme.qml).

### Base Ground & Elevation System
Warm near-blacks replace cold OLED blue-blacks. This prevents amber focus rings from reading as "error/warning" states and complements warm film photography.

| Role | Token in `Theme.qml` | Hex | RGB | Usage |
| :--- | :--- | :--- | :--- | :--- |
| **Ground** | `theme.ground` | `#0C0B0A` | `12, 11, 10` | Fullscreen window ground, root canvas |
| **Surface** | `theme.surfaceColor` | `#141210` | `20, 18, 16` | Media cards, navigation rail, inactive chrome |
| **Surface Raised** | `theme.surfaceRaisedColor` | `#1D1A17` | `29, 26, 23` | Hovered cards, OSD bottom sheets, popovers |
| **Surface Overlay** | `theme.surfaceOverlay` | `#262220` | `38, 34, 32` | Dialogs, context menus, command palette |
| **Hairline** | `theme.hairline` | `#2E2A26` | `46, 42, 38` | 1 px card boundaries, separators |

### The Accent Palette: "Projection Amber"
Projection Amber is the signature brand color. Only **Accent Core** (`theme.accentColor`)
and **Accent Muted** (`theme.accentMuted`) are actual `Theme.qml` tokens the app reads at
runtime; **Accent Bright** and **Accent Deep** exist solely as gradient stops inside
`scripts/generate_brand_assets.py` (`COLORS[accent]["bright"]`/`["deep"]`) used to draw the
brand artwork's amber gradient — there is no `theme.accentBright` or `theme.accentDeep` in
the app itself.

| Swatch | Role | Hex | RGB | Purpose |
| :--- | :--- | :--- | :--- | :--- |
| `#F0A02A` | **Accent Core** | `#F0A02A` | `240, 160, 42` | Focus rings, playback progress, active tab |
| `#FFBE53` | **Accent Bright** *(asset-generator gradient stop only)* | `#FFBE53` | `255, 190, 83` | Gradient highlight in brand artwork |
| `#C87A10` | **Accent Deep** *(asset-generator gradient stop only)* | `#C87A10` | `200, 122, 16` | Gradient shadow in brand artwork |
| `#7A5416` | **Accent Muted** | `#7A5416` | `122, 84, 22` | Seek bar inactive track, disabled toggles |

### Secondary Accent Variants (Server Matching)
StrmQt ships with 3 server-matching alternates configurable in Settings:
* **Emby Emerald**: `#52B54B` (Muted: `#2C5C29`) — First-party Emby client familiarity.
* **Breeze Cyan**: `#3DAEE9` (Muted: `#1E5872`) — KDE Plasma desktop cohesion.
* **Jellyfin Violet**: `#AA5CC3` (Muted: `#553063`) — Jellyfin ecosystem compatibility.

### Typography Ink
| Role | Token in `Theme.qml` | Hex | Purpose |
| :--- | :--- | :--- | :--- |
| **Text Primary** | `theme.textPrimaryColor` | `#F5F1EA` | Titles, item headers, active labels |
| **Text Secondary** | `theme.textSecondaryColor` | `#A29A8E` | Subtitles, artists, genres, plot overview |
| **Text Tertiary** | `theme.textTertiary` | `#928A80` | Durations, timecodes, badge labels |
| **Text Disabled** | `theme.textDisabled` | `#4A453F` | Inactive controls, placeholder copy |

---

## 5. Typography System

All typefaces are bundled inside the binary (`assets/fonts` -> `qrc:/fonts`) so sandboxed Flatpaks and AppImages render with 100% fidelity on any Linux distribution without depending on host font caches.

```
       Display: Archivo (DemiBold; Crate music: wdth 120 / wght 820)
       Body:    Public Sans (Medium, Regular)
       Data:    IBM Plex Mono (Regular, Tabular)
```

1. **Display & Headings: Archivo**
   * *Role*: Section headers, hero titles, marquee titles, shelf titles.
   * *Characteristics*: High legibility across a living room at 10 feet. Wide is a Crate
     (music) trait, not a whole-app one — see below.
   * *Weights*: DemiBold (`600`) is what every non-music `Theme.fontDisplay` call site
     sets — checked all of them. **Crate (music) is the exception**: its display type
     (`Theme.crateDisplayAxes`/`crateDisplayWeight`, `Theme.qml`) pushes the same variable
     font to width axis `120` and weight axis `820` — genuinely Heavy, not DemiBold — and
     that combination is requested nowhere else. `CrateHeading.qml` is the single place
     that sets it, and every Crate hero title (Home, Album, Artist, Playlist), shelf
     heading, section strip and genre bin goes through it, in uppercase with tightened
     (`-0.02em`) tracking. Outside Crate, Archivo never has its width axis touched at all
     — `font.variableAxes` is set nowhere else in `src/ui`.

2. **Body & Controls: Public Sans**
   * *Role*: Card titles, navigation buttons, descriptions, settings controls, toasts.
   * *Characteristics*: Open apertures, generous x-height, neutral, honest tabular numerals.
   * *Weights*: Regular (`400`), Medium (`500`), SemiBold (`600`).

3. **Data & Technical Overlays: IBM Plex Mono**
   * *Role*: Playback timecodes (`01:24:18 / 02:15:00`), video codec tags (`HEVC 4K`, `AV1`), audio bitrate readouts (`FLAC 96kHz/24bit`), and hwdec statistics.
   * *Characteristics*: Monospace precision, zero jitter on timecode increments.
   * *Weight*: Regular — every timecode, codec chip and bitrate readout renders through
     `Theme.fontMono` with no weight override. `IBMPlexMono-Medium.ttf` and
     `IBMPlexMono-SemiBold.ttf` are bundled and registered (`src/CMakeLists.txt`) but
     `Theme.qml` exposes no `fontMonoMedium`/`fontMonoSemiBold` token for them; the two
     places in `SettingsPage.qml` that set `font.weight` on mono text are keybinding
     labels, not part of this role.

4. **Crate: Music's typographic dialect**
   * Music (the Home/Browse/Album/Artist/Playlist "Crate" pages, `src/ui/music/`,
     `src/ui/pages/Music*Page.qml`) reuses the same three typefaces, ground and accent as
     the rest of the app — no new colours, per `Theme.qml`'s own comment — but sets
     Archivo louder for anything that speaks as a heading: `Theme.crateDisplayWeight`
     (`820`) and `Theme.crateDisplayAxes` (`{ wdth: 120, wght: 820 }`), applied through
     `CrateHeading.qml` alone. Data stays in Plex Mono, unchanged from the rest of the app.

---

## 6. Application Icon & Desktop Integration

The application icon is registered under the AppStream component ID `ca.mikesdev.StrmQt`.

### Canonical App Icon Specifications
* **Master Vector**: [`assets/icons/ca.mikesdev.StrmQt.svg`](../assets/icons/ca.mikesdev.StrmQt.svg)
* **Squircle Geometry**: 512 x 512 px canvas with `rx="112" ry="112"` corner curvature conforming to modern Linux FreeDesktop / KDE Plasma HIG.
* **Composition**:
  1. Base squircle in `#141210` -> `#0C0B0A` gradient with `#2E2A26` hairline stroke.
  2. Warm Projector Beam radial glow emanating from the optical center.
  3. Concentric media seek track (`#262220`) with a 270° Projection Amber progress arc.
  4. Four perimeter lens calibration marks at 0°, 90°, 180°, and 270°.
  5. The Aperture-Q iris blades with the integrated amber play triangle.

### Pre-Rendered Hicolor PNG Assets
Pre-rendered PNGs are maintained for distributions and window managers requiring bitmap icon caches:
* `assets/icons/hicolor/32x32/apps/ca.mikesdev.StrmQt.png`
* `assets/icons/hicolor/48x48/apps/ca.mikesdev.StrmQt.png`
* `assets/icons/hicolor/64x64/apps/ca.mikesdev.StrmQt.png`
* `assets/icons/hicolor/128x128/apps/ca.mikesdev.StrmQt.png`
* `assets/icons/hicolor/256x256/apps/ca.mikesdev.StrmQt.png`
* `assets/icons/hicolor/512x512/apps/ca.mikesdev.StrmQt.png`

Regenerate all brand assets and icon sizes anytime with:
```bash
pip install matplotlib      # provides font_manager + TextPath used to trace the wordmark
# rsvg-convert (librsvg) must also be on PATH — it rasterizes the hicolor PNGs
python3 scripts/generate_brand_assets.py
```
Both are generator-only tooling dependencies: nothing the compiled app links against, so
they carry no CMake or runtime footprint. The script also assumes it is run from a checkout
that has `assets/fonts/` populated (the same TTFs `src/CMakeLists.txt` bundles), since it
sets the wordmark glyphs from those files directly rather than a system font.

---

## 7. UI Principles: The 10-Foot Experience

1. **Hover is NOT Focus**:
   * *Hover* indicates where the mouse pointer rests: it applies a subtle surface lift (`+10%` white tint) and shows tooltips. It **never** moves keyboard focus or steals cursor position.
   * *Focus* indicates where the gamepad or D-pad is parked: it renders the full-strength **3px Projection Amber ring**.
   * Focus always wins over hover visually.

2. **Density Scaling**:
   * StrmQt operates across three display densities:
     * `compact` (0.9x): Dense desktop monitor viewing.
     * `comfortable` (1.0x): Default desktop & laptop.
     * `tv` (1.15x): 10-foot / couch viewing.
   * All three are a manual choice — the "Interface density" select in Settings sets
     `Theme.densityMode` directly (`SettingsPage.qml`) and it persists through `Settings`.
     Nothing in `src/input` switches it automatically when a gamepad wakes up; that
     wiring does not exist today.

3. **Motion Curves**:
   * Pointer hover is instant (`90 ms` `Easing.OutQuad`) so cursor tracking never feels sluggish.
   * Focus ring steps glide deliberately (`160 ms` `Easing.OutCubic`).
   * When `reducedMotion` is enabled, all interaction transition durations collapse to `0 ms`.

---

## 8. Brand Do's & Don'ts

### Do:
* **Do** use the official Projection Amber `#F0A02A` on warm-black `#0C0B0A` for primary branding.
* **Do** use the vector paths provided in `assets/brand/` for marketing banners, store packaging, and documentation.
* **Do** keep adequate clear space around the logo mark.
* **Do** respect the distinction between Amber for focus and semantic colors (`#6FBF73` watched, `#E05C5C` error, `#E0A33E` transcoding).

### Don't:
* **Don't** use cold pure blue or generic OLED black `#000000` as the ground canvas.
* **Don't** stretch, skew, or rotate the Aperture-Q symbol independently of the logotype.
* **Don't** add drop shadows or bevels to the flat vector logotype.
* **Don't** spell the name as "STRMQT" or "StreamQt".
* **Don't** wire pointer hover events to trigger amber focus rings.
