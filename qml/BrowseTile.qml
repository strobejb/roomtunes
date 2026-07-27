import QtQuick
import QtQuick.Effects

// One icon+label tile in BrowseHome.qml's "Recently Played"/"Favourites"/
// "Your Services" sections -- a compact icon-on-top, label-below shape
// (unlike MusicServiceRow.qml's full-width icon+name+chevron row), laid
// out 4-per-row in a wrapping GridLayout by the caller. Same
// Image+mask+MultiEffect icon-masking pattern as MusicServiceRow.qml, just
// square-tile-shaped rather than row-shaped.
Item {
    id: root

    property string title: ""
    property string imageUrl: ""
    // Circular for service logos (brand marks read better round, matching
    // MusicServiceRow.qml's own root-services-list default); off for
    // favourites/recently-played, whose art is usually rectangular album/
    // playlist covers.
    property bool circularIcon: true

    signal clicked()

    implicitWidth: 76
    implicitHeight: 104

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    Rectangle {
        anchors.top: parent.top
        anchors.topMargin: -8
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width
        height: Math.min(parent.height - anchors.topMargin, iconArea.height + titleLabel.anchors.topMargin
                         + Math.ceil(titleLabel.font.pixelSize * 1.25) * 2 + 16)
        radius: 12
        color: "#E8E8E8"
        visible: mouseArea.containsMouse
    }

    Item {
        id: iconArea
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: 48
        height: 48

        Rectangle {
            anchors.fill: parent
            radius: root.circularIcon ? width / 2 : 8
            color: "#BDBDBD"
            visible: iconImage.status !== Image.Ready
        }

        // Never shown directly -- MultiEffect below reads its pixels
        // through `source:` and applies the mask (see MusicServiceRow.qml's
        // identical pattern).
        Image {
            id: iconImage
            anchors.fill: parent
            source: root.imageUrl
            sourceSize.width: width
            sourceSize.height: height
            asynchronous: true
            smooth: true
            mipmap: true
            visible: false
        }

        Item {
            id: maskShape
            anchors.fill: parent
            layer.enabled: true
            visible: false

            Rectangle {
                anchors.fill: parent
                radius: root.circularIcon ? width / 2 : 8
                color: "black"
            }
        }

        MultiEffect {
            anchors.fill: parent
            visible: iconImage.status === Image.Ready
            source: iconImage
            maskEnabled: true
            maskSource: maskShape
        }

        Text {
            anchors.centerIn: parent
            visible: iconImage.status !== Image.Ready
            text: "♪"
            font.pixelSize: 18
            color: "#7A7A7A"
        }
    }

    Text {
        id: titleLabel
        anchors.top: iconArea.bottom
        anchors.topMargin: 6
        anchors.left: parent.left
        anchors.right: parent.right
        text: root.title
        font.pixelSize: 12
        color: "#212121"
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        maximumLineCount: 2
        wrapMode: Text.WordWrap
    }
}
