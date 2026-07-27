import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Pushed by BrowseHome.qml's "Your Services" section header chevron --
// every installed service (musicServiceModel in full), not just the
// handful of tiles shown on the home page. Otherwise identical to the
// plain vertical services list BrowseStack.qml used to show as its whole
// root page before BrowseHome.qml replaced it.
Item {
    id: root

    property StackView stack
    property Component pageComponent

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
                text: qsTr("Your Services")
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
            model: musicServiceModel

            delegate: MusicServiceRow {
                width: listView.width - 20
                title: model.title
                imageUrl: model.imageUrl

                onClicked: {
                    browseRecency.recordUse(model.serviceKey)
                    root.stack.pushFolder(root.pageComponent, {
                        title: model.title,
                        service: model.serviceObject,
                        objectId: "root",
                        stack: root.stack,
                        pageComponent: root.pageComponent
                    })
                }
            }

            Label {
                anchors.centerIn: parent
                visible: listView.count === 0
                text: qsTr("No music services found")
                color: "#9E9E9E"
                font.pixelSize: 13
            }
        }
    }
}
