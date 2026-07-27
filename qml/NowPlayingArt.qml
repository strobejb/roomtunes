import QtQuick
import QtQuick.Effects

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
        color: "#BDBDBD"
        visible: artImage.status !== Image.Ready
    }

    // Never shown directly -- MultiEffect below reads its pixels through
    // `source:` and applies the mask. clip:true on a plain Rectangle only
    // clips to its bounding box, not its own radius, so a masked Image is
    // the only way to actually get rounded corners (same reasoning as
    // MusicServiceRow.qml's icon masking/BrowseListPage.qml's folder-art
    // header, reused verbatim here).
    Image {
        id: artImage
        anchors.fill: parent
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        source: root.imageUrl
        smooth: true
        mipmap: true
        visible: false
    }

    Item {
        id: artMask
        anchors.fill: parent
        layer.enabled: true
        visible: false

        Rectangle {
            anchors.fill: parent
            radius: 8
            color: "black"
        }
    }

    MultiEffect {
        anchors.fill: parent
        visible: artImage.status === Image.Ready
        source: artImage
        maskEnabled: true
        maskSource: artMask
    }

    Text {
        anchors.centerIn: parent
        visible: artImage.status !== Image.Ready
        text: "♪"
        font.pixelSize: parent.width * 0.33
        color: "#7A7A7A"
    }
}
