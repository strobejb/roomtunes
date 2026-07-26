import QtQuick
import QtQuick.Layouts

// The narrow-panel Now Playing arrangement: album art centered at the
// top, title/artist/scrub-bar block beneath it (also centered), transport
// controls at the bottom. Shown instead of NowPlayingWide.qml once the
// panel gets too narrow for that layout's transport controls row to fit
// without clipping (see NowPlayingPanel.qml's useCompactLayout) --
// typically a narrow window/column, but the same trigger applies equally
// on an actual small/mobile-sized screen.
//
// Unlike NowPlayingWide.qml, this panel's *height* isn't fixed while this
// is showing -- NowPlayingPanel.qml reads this layout's own implicitHeight
// (a ColumnLayout computes that from its children regardless of whatever
// height it's actually been given) and grows Main.qml's Panel to fit,
// rather than cramming art+text+controls into the wide layout's normal
// 280px. That's why this is a plain top-to-bottom flow with ordinary
// spacing -- no fillHeight spacer pinning the controls to a fixed-size
// bottom edge -- the controls row just naturally ends up at the bottom
// because it's the last child.
ColumnLayout {
    id: root

    property var zone // ZonePlayer* -- null when no zone is selected
    property var track // MediaItem* -- null when nothing's loaded
    property bool isPlaying: false
    property bool backgroundIsLight: true
    property color contrastColor: "#212121"
    property color buttonFillColor: "#212121"
    property color buttonIconColor: "white"
    property color controlHoverColor: Qt.rgba(0, 0, 0, 0.1)
    property color controlPressedColor: Qt.rgba(0, 0, 0, 0.2)
    property var blendToward

    // Overridden by NowPlayingPanel.qml with the real measured
    // controlsProbe.implicitWidth -- the 344 default here is only a
    // fallback for standalone preview/testing, so the sizeScale
    // calculation below and NowPlayingPanel's own minimumCompactWidth
    // always agree on the same real number instead of two separately
    // hardcoded approximations of it.
    property real fullControlsWidth: 344
    // Overridden by NowPlayingPanel.qml with its own compactSizeFloor,
    // for the same single-source-of-truth reason.
    property real sizeFloor: 0.9

    spacing: 16

    // A Layout.fillWidth wrapper with the art anchor-centered inside it,
    // rather than Layout.alignment: Qt.AlignHCenter directly on the art
    // item itself -- confirmed via a debug build that Qt Quick Layouts
    // centers a non-fillWidth child against this ColumnLayout's own
    // *implicit* width (the natural size it would want if nothing
    // constrained it -- here, NowPlayingTransportControls' fixed 344px
    // footprint, the widest thing in this column) rather than its actual,
    // externally-constrained width (this whole layout is anchors.fill'd
    // to whatever room NowPlayingPanel.qml's panel actually has, often
    // much less than 344px). The art rendered 124px from the left in a
    // 180px-wide panel as a result -- exactly (344-96)/2, i.e. centered
    // in a column 164px wider than it really was, overflowing off the
    // right edge. A fillWidth wrapper *does* correctly receive the real
    // constrained width, so anchoring within it sidesteps the bug
    // entirely.
    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 96
        Layout.topMargin: 8

        NowPlayingArt {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 96
            height: 96
            imageUrl: root.track && root.track.imageUrl ? root.track.imageUrl : ""
        }
    }

    ColumnLayout {
        id: textBlock
        Layout.fillWidth: true
        spacing: 4

        // Layout.minimumWidth: 0 on both Text items below -- elide alone
        // doesn't stop a Text's implicitWidth from reporting the full
        // unwrapped string's natural width, and Qt Quick Layouts treats
        // that implicitWidth as an effective floor under fillWidth
        // shrinking unless minimumWidth is explicitly zeroed. Without it,
        // a long track title forces this whole column (and everything
        // fillWidth beneath it, like the scrub bar) wider than root, and
        // the overflow renders past the panel's edge instead of eliding.
        Text {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: root.track && root.track.title
                  ? root.track.title
                  : (root.zone ? qsTr("Nothing playing") : qsTr("No zone selected"))
            font.pixelSize: 18
            font.weight: Typography.emphasisWeight
            color: root.contrastColor
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: root.track ? root.track.artist : ""
            font.pixelSize: 13
            color: root.contrastColor
            opacity: 0.65
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
        }

        NowPlayingScrubBar {
            Layout.fillWidth: true
            Layout.topMargin: 6
            visible: root.track !== null
            zone: root.zone
            contrastColor: root.contrastColor
        }
    }

    NowPlayingTransportControls {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 4
        Layout.bottomMargin: 8
        // Shrinks to whatever width this column actually has, down to
        // root.sizeFloor -- deliberately close to 1.0 (barely
        // perceptible shrink) rather than a dramatic one, since this is
        // the app's primary content and shouldn't be the thing that
        // gives way when the window narrows (see Main.qml's
        // nowPlayingColumn Layout.minimumWidth, which keeps this column
        // from ever actually reaching root.sizeFloor in practice --
        // Zones absorbs the squeeze instead). root.width, not this
        // row's own width, since Layout.fillWidth means this row's
        // *actual* rendered width is dictated by root (top-down) rather
        // than the other way around, so referencing it here isn't
        // circular.
        sizeScale: Math.max(root.sizeFloor, Math.min(1.0, root.width / root.fullControlsWidth))
        zone: root.zone
        isPlaying: root.isPlaying
        backgroundIsLight: root.backgroundIsLight
        buttonFillColor: root.buttonFillColor
        buttonIconColor: root.buttonIconColor
        controlHoverColor: root.controlHoverColor
        controlPressedColor: root.controlPressedColor
        blendToward: root.blendToward
    }
}
