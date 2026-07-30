import QtQuick

// One icon+label tile in BrowseHome.qml's "Recently Played"/"Favourites"/
// "Your Services" sections -- a compact icon-on-top, label-below shape
// (unlike MusicServiceRow.qml's full-width icon+name+chevron row), laid
// out 4-per-row in a wrapping GridLayout by the caller. Same
// RoundedImage handles clipping natively; Qt Quick's shader masks were too
// brittle for these small service/track icons.
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
        antialiasing: true
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
            antialiasing: true
            layer.enabled: true
            layer.samples: 4
            color: "#BDBDBD"
            visible: iconImage.status !== RoundedImage.Ready
        }

        RoundedImage {
            id: iconImage
            anchors.fill: parent
            source: root.imageUrl
            radius: root.circularIcon ? width / 2 : 8
            visible: status === RoundedImage.Ready
        }

        Text {
            anchors.centerIn: parent
            visible: iconImage.status !== RoundedImage.Ready
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
