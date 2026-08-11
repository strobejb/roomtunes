import QtQuick
import QtQuick.Layouts

// The original Now Playing arrangement: album art to the left, title/
// artist/scrub-bar column to its right, transport controls centered in
// their own row below. Used whenever NowPlayingPanel.qml's panel is wide
// enough for the transport controls row to fit without clipping; below
// that threshold NowPlayingCompact.qml is shown instead.
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
    // Reserves this much space on the title/scrub-bar column's right edge
    // (see NowPlayingPanel.qml's volumeReserveWidth) so the corner volume
    // pill never overlaps them, and hides their content outright while
    // the pill is actually expanded (they'd otherwise sit directly
    // underneath its revealed slider).
    property int volumeReserveWidth: 0
    property bool volumeExpanded: false

    spacing: 24

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 16
        spacing: 16

        NowPlayingArt {
            Layout.preferredWidth: 120
            Layout.preferredHeight: 120
            imageUrl: root.track && root.track.imageUrl ? root.track.imageUrl : ""
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 4

            // Layout.minimumWidth: 0 on both Text items below -- elide
            // alone doesn't stop a Text's implicitWidth from reporting the
            // full unwrapped string's natural width, and Qt Quick Layouts
            // treats that implicitWidth as an effective floor under
            // fillWidth shrinking unless minimumWidth is explicitly
            // zeroed. Without it, a long enough track title would force
            // this column wider than its real available space instead of
            // eliding (see the same fix in NowPlayingCompact.qml, where a
            // short "stupid song" title already reproduced the overflow
            // at narrow widths).
            ScrollingText {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                // Always reserves the volume pill's width rather than
                // only once the pill is actually expanded, so opening it
                // never covers a chunk of title that was visible a moment
                // before -- the truncation point is fixed either way.
                Layout.rightMargin: root.volumeReserveWidth
                text: root.track && root.track.title
                      ? root.track.title
                      : (root.zone ? qsTr("Nothing playing") : qsTr("No zone selected"))
                pixelSize: 20
                weight: Typography.emphasisWeight
                color: root.contrastColor
            }

            ScrollingText {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.track ? root.track.artist : ""
                pixelSize: 14
                color: root.contrastColor
                textOpacity: 0.65
            }

            NowPlayingScrubBar {
                Layout.fillWidth: true
                Layout.rightMargin: root.volumeReserveWidth
                Layout.topMargin: 6
                visible: root.track !== null
                zone: root.zone
                contrastColor: root.contrastColor
                hideForVolume: root.volumeExpanded
            }
        }
    }

    NowPlayingTransportControls {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignHCenter
        zone: root.zone
        track: root.track
        isPlaying: root.isPlaying
        backgroundIsLight: root.backgroundIsLight
        buttonFillColor: root.buttonFillColor
        buttonIconColor: root.buttonIconColor
        controlHoverColor: root.controlHoverColor
        controlPressedColor: root.controlPressedColor
        blendToward: root.blendToward
    }
}
