import QtQuick

// Masked, rounded-corner album art -- shared between NowPlayingWide.qml
// (left of the title/artist/scrub column) and NowPlayingCompact.qml
// (centered above it). Caller sizes it via width/height (or
// Layout.preferredWidth/Height if placed in a Layout); this only ever
// renders as a square.
Item {
    id: root

    property string imageUrl: ""

    Rectangle {
        anchors.fill: parent
        radius: 8
        antialiasing: true
        layer.enabled: true
        layer.samples: 4
        color: "#BDBDBD"
        visible: artImage.status !== RoundedImage.Ready
    }

    RoundedImage {
        id: artImage
        anchors.fill: parent
        source: root.imageUrl
        radius: 8
        visible: status === RoundedImage.Ready
    }

    Text {
        anchors.centerIn: parent
        visible: artImage.status !== RoundedImage.Ready
        text: "♪"
        font.pixelSize: parent.width * 0.33
        color: "#7A7A7A"
    }
}
