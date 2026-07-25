import QtQuick
import QtQuick.Layouts
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
    implicitHeight: 88

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        Item {
            id: iconArea
            Layout.preferredWidth: 48
            Layout.preferredHeight: 48
            Layout.alignment: Qt.AlignHCenter

            // Always a rounded rectangle (not circular even when the icon
            // itself is), sized bigger than the icon and centered behind
            // it -- declared first so it paints underneath everything
            // else in this Item, with the icon drawn on top of it.
            Rectangle {
                anchors.centerIn: parent
                width: parent.width + 16
                height: parent.height + 16
                radius: 12
                color: "#E8E8E8"
                visible: mouseArea.containsMouse
            }

            Rectangle {
                anchors.fill: parent
                radius: root.circularIcon ? width / 2 : 8
                color: "#E8E8E8"
                visible: iconImage.status !== Image.Ready
            }

            // Never shown directly -- MultiEffect below reads its pixels
            // through `source:` and applies the mask (see
            // MusicServiceRow.qml's identical pattern).
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
                color: "#BDBDBD"
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            text: root.title
            font.pixelSize: 12
            color: "#212121"
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            maximumLineCount: 2
            wrapMode: Text.WordWrap
        }
    }
}
