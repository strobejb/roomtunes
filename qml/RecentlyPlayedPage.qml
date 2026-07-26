import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Pushed by BrowseHome.qml's "Recently Played" section header chevron --
// the full history (up to RecentlyPlayedModel's own cap), not just the
// handful of tiles shown on the home page. Plain vertical list, not
// MusicServiceRow (which has no artist subtext slot and assumes a
// browse/menu-driven row) -- a recently-played row is simpler: tap it,
// it plays, that's the whole interaction.
Item {
    id: root

    property StackView stack

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Item {
            id: header
            Layout.fillWidth: true
            implicitHeight: 32

            Item {
                id: backButton
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 32
                height: 32

                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: backMouseArea.containsMouse ? "#F0F0F0" : "transparent"
                }

                Image {
                    anchors.centerIn: parent
                    source: "../resources/icons/chevron_left.svg"
                    sourceSize.width: 26
                    sourceSize.height: 26
                }

                MouseArea {
                    id: backMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.stack.goBack()
                }
            }

            Label {
                anchors.left: backButton.right
                anchors.leftMargin: 4
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Recently Played")
                font.pixelSize: Math.round(16 * UiScale.factor)
                font.weight: Typography.emphasisWeight
                color: "#212121"
                elide: Text.ElideRight
            }
        }

        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            spacing: 4
            model: recentlyPlayedModel

            delegate: MusicServiceRow {
                width: listView.width - 20
                title: model.title
                imageUrl: model.imageUrl
                showChevron: false
                circularIcon: false

                onClicked: {
                    if (root.stack.zone)
                        root.stack.zone.playItem(model.item)
                }
            }

            Label {
                anchors.centerIn: parent
                visible: listView.count === 0
                text: qsTr("Nothing played yet")
                color: "#9E9E9E"
                font.pixelSize: 13
            }
        }
    }
}
