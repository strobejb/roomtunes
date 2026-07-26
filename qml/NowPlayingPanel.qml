import QtQuick

// Album art + track/artist + transport controls for the currently selected
// zone/group. Owns all the shared state (selected zone, colors derived
// from the track's album art, position polling) and picks which of two
// layouts actually renders it: NowPlayingWide.qml (album art beside the
// title/artist/scrub column, the original arrangement) when there's room
// for its transport controls row to fit without clipping, otherwise
// NowPlayingCompact.qml (art centered on top, controls pinned to the
// bottom) -- see useCompactLayout below for exactly how that's decided.
// Shuffle/repeat are visually present but not wired to a backend call yet
// -- ZonePlayer has no play-mode (SetPlayMode) support.
Item {
    id: root

    property var zone // ZonePlayer* -- the selected group's coordinator

    // "minimal": fixed white background, dark text/icons (the original
    // look). "color": the panel's background is a prominent color picked
    // from the current track's album art (ZonePlayer::accentColor, see
    // AlbumColorAnalyzer), changing per track -- text/icon/button colors
    // are recomputed below to stay legible against whatever that color
    // turns out to be.
    property string renderMode: "color"

    readonly property var track: zone ? zone.currentTrack : null
    readonly property bool isPlaying: zone ? zone.playStateText === "Playing" : false

    readonly property color minimalBackground: "white"
    readonly property bool hasAccentColor: !!zone && zone.accentColor.a > 0
    readonly property color panelBackgroundColor:
        (renderMode === "color" && hasAccentColor) ? zone.accentColor : minimalBackground

    readonly property bool backgroundIsLight: luminance(panelBackgroundColor) > 0.55

    // Regular text/icons: whichever of black/white reads clearly against
    // the panel background.
    readonly property color contrastColor: backgroundIsLight ? "#212121" : "white"
    // The central play button is the odd one out: its *fill* matches
    // contrastColor (reading as a deliberate solid accent against the
    // panel, the same relationship the original white-background design
    // had -- white bg, dark text, dark button), while its *icon* is
    // contrastColor's inverse, for legibility against that fill.
    readonly property color buttonFillColor: contrastColor
    readonly property color buttonIconColor: backgroundIsLight ? "white" : "#212121"

    function luminance(c) {
        return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b
    }

    function blendToward(base, target, amount) {
        return Qt.rgba(
            base.r + (target.r - base.r) * amount,
            base.g + (target.g - base.g) * amount,
            base.b + (target.b - base.b) * amount,
            1.0)
    }

    // Hover/press feedback for the icon buttons surrounding the panel
    // (shuffle/skip/repeat/volume): a lighter shade of the panel background
    // on a dark background, a darker shade on a light one -- an opaque
    // blend of panelBackgroundColor itself, not a translucent overlay, so
    // it still reads as "the background, subtly highlighted" rather than a
    // generic grey wash regardless of what color the background actually is.
    readonly property color controlHoverColor: backgroundIsLight
        ? blendToward(panelBackgroundColor, Qt.rgba(0, 0, 0, 1), 0.12)
        : blendToward(panelBackgroundColor, Qt.rgba(1, 1, 1, 1), 0.18)
    readonly property color controlPressedColor: backgroundIsLight
        ? blendToward(panelBackgroundColor, Qt.rgba(0, 0, 0, 1), 0.22)
        : blendToward(panelBackgroundColor, Qt.rgba(1, 1, 1, 1), 0.30)

    // Horizontal space NowPlayingWide.qml's title/scrub column reserves on
    // its right so its own truncation point never moves once the volume
    // pill opens -- the pill is always 44px wide (it only ever grows
    // *downward*), so this is a fixed amount regardless of expanded state:
    // the gap between the title column's own right inset (this panel's
    // 20px margin) and the volume icon's left edge (its 44px width + 16px
    // right margin).
    readonly property int volumeReserveWidth: volumeControl.width + volumeControl.anchors.rightMargin - 20

    // Below this width, NowPlayingWide.qml's transport controls row would
    // start clipping against the panel's own margins -- switch to
    // NowPlayingCompact.qml instead. controlsProbe is a real (if
    // invisible) instance of the exact same row shown in either layout;
    // every icon in it is a fixed size regardless of track/zone data, so
    // its implicitWidth is a stable, always-accurate measurement rather
    // than a guessed/hardcoded threshold that'd drift out of sync if the
    // row's own contents ever change.
    readonly property bool useCompactLayout: width < (controlsProbe.implicitWidth + 40)

    // How much NowPlayingCompact.qml's transport controls are allowed to
    // shrink by, at most -- deliberately close to 1.0 (see its own
    // sizeScale comment for why: this is the app's primary content, not
    // where a narrowing window should spend its squeeze).
    readonly property real compactSizeFloor: 0.9

    // The narrowest this panel is allowed to get once Compact is
    // showing, in real pixels -- Main.qml binds nowPlayingColumn's own
    // Layout.minimumWidth to this, so Qt Quick Layouts gives Zones (see
    // Main.qml's zonesColumn, which already shrinks via UiScale.factor)
    // the squeeze instead of this panel once compactSizeFloor is
    // reached. +40 for the same anchors.margins: 20-per-side both
    // NowPlayingWide.qml and NowPlayingCompact.qml are shown with below.
    readonly property int minimumCompactWidth: Math.ceil(controlsProbe.implicitWidth * compactSizeFloor) + 40

    // How tall this panel wants to be -- Main.qml binds its wrapping
    // Panel's own Layout.preferredHeight to this instead of a flat
    // constant, since NowPlayingCompact.qml's stacked (art-over-text-over-
    // controls) arrangement genuinely needs more vertical room than
    // NowPlayingWide.qml's side-by-side one to show its transport controls
    // without clipping them -- unlike Wide, Compact doesn't fight for
    // space inside a fixed box (see its own class comment). compactLayout
    // is a real ColumnLayout, so its implicitHeight is always its
    // children's actual natural height, computed the same way regardless
    // of whether Compact happens to be the visible one right now.
    readonly property int preferredHeight: useCompactLayout ? (compactLayout.implicitHeight + 40) : 280

    NowPlayingTransportControls {
        id: controlsProbe
        visible: false
    }

    // GetPositionInfo isn't event-driven (no AVTransport GENA subscription
    // exists in this app), so polling once a second while actually playing
    // is what keeps the scrub bar moving -- ZonePlayer itself skips
    // rebuilding currentTrack when a poll's track hasn't changed, so this
    // doesn't cause the churn it otherwise would.
    Timer {
        interval: 1000
        repeat: true
        running: root.zone !== null && root.isPlaying
        onTriggered: root.zone.refreshTransportState()
    }

    NowPlayingWide {
        anchors.fill: parent
        anchors.margins: 20
        visible: !root.useCompactLayout
        zone: root.zone
        track: root.track
        isPlaying: root.isPlaying
        backgroundIsLight: root.backgroundIsLight
        contrastColor: root.contrastColor
        buttonFillColor: root.buttonFillColor
        buttonIconColor: root.buttonIconColor
        controlHoverColor: root.controlHoverColor
        controlPressedColor: root.controlPressedColor
        blendToward: root.blendToward
        volumeReserveWidth: root.volumeReserveWidth
        volumeExpanded: volumeControl.expanded
    }

    NowPlayingCompact {
        id: compactLayout
        anchors.fill: parent
        anchors.margins: 20
        visible: root.useCompactLayout
        zone: root.zone
        track: root.track
        isPlaying: root.isPlaying
        backgroundIsLight: root.backgroundIsLight
        contrastColor: root.contrastColor
        buttonFillColor: root.buttonFillColor
        buttonIconColor: root.buttonIconColor
        controlHoverColor: root.controlHoverColor
        controlPressedColor: root.controlPressedColor
        blendToward: root.blendToward
        fullControlsWidth: controlsProbe.implicitWidth
        sizeFloor: root.compactSizeFloor
    }

    // Sits above both layouts rather than inside either one, so it stays
    // pinned to the panel's actual top-right corner regardless of which
    // is active or how tall its title/artist/scrub-bar block grows.
    NowPlayingVolumeControl {
        id: volumeControl
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 16
        anchors.rightMargin: 16
        zone: root.zone
        backgroundIsLight: root.backgroundIsLight
        contrastColor: root.contrastColor
        pillColor: root.panelBackgroundColor
        controlHoverColor: root.controlHoverColor
        controlPressedColor: root.controlPressedColor
    }
}
