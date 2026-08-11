import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

Popup {
    id: menu

    property var zone: null
    property var track: null
    property string musicServiceName: qsTr("Music Service")
    property bool countedOpen: false

    function openFor(button) {
        parent = button
        x = button.width - width
        y = button.height + 6
        open()
    }

    function updateOpenCount() {
        if (visible && !countedOpen) {
            ActionMenuState.openCount += 1
            countedOpen = true
        } else if (!visible && countedOpen) {
            ActionMenuState.openCount = Math.max(0, ActionMenuState.openCount - 1)
            countedOpen = false
        }
    }

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 6
    width: 254

    onVisibleChanged: updateOpenCount()
    Component.onDestruction: {
        if (countedOpen)
            ActionMenuState.openCount = Math.max(0, ActionMenuState.openCount - 1)
    }

    Overlay.modal: Item {}

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: 180; easing.type: Easing.OutQuad }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: 140; easing.type: Easing.InQuad }
    }

    background: Rectangle {
        radius: 10
        color: "white"

        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: "#33000000"
            shadowBlur: 0.6
            shadowVerticalOffset: 4
        }
    }

    contentItem: ColumnLayout {
        spacing: 2

        MenuTextRow {
            text: qsTr("Save to Sonos Favourites")
            onClicked: {
                if (menu.zone)
                    menu.zone.addCurrentTrackToSonosFavourites()
                menu.close()
            }
        }

        MenuTextRow {
            text: qsTr("Add to Sonos Playlist")
        }

        Item {
            Layout.fillWidth: true
            implicitHeight: 32

            Rectangle {
                anchors.fill: parent
                radius: 6
                color: crossfadeMouseArea.pressed
                    ? "#D0D0D0"
                    : (crossfadeMouseArea.containsMouse ? "#F0F0F0" : "transparent")
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Crossfade")
                font.pixelSize: 13
                color: "#212121"
            }

            Rectangle {
                id: crossfadeSwitch
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                width: 34
                height: 18
                radius: height / 2
                antialiasing: true
                color: menu.zone && menu.zone.crossfadeEnabled ? "#4CAF50" : "#D0D0D0"

                Behavior on color {
                    ColorAnimation { duration: 120 }
                }

                Rectangle {
                    width: 14
                    height: 14
                    radius: 7
                    antialiasing: true
                    color: "white"
                    y: 2
                    x: menu.zone && menu.zone.crossfadeEnabled ? crossfadeSwitch.width - width - 2 : 2

                    Behavior on x {
                        NumberAnimation { duration: 120; easing.type: Easing.OutQuad }
                    }
                }
            }

            MouseArea {
                id: crossfadeMouseArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (menu.zone)
                        menu.zone.setCrossfadeEnabled(!menu.zone.crossfadeEnabled)
                }
            }
        }

        Item {
            Layout.fillWidth: true
            implicitHeight: 9

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: 1
                color: "#E0E0E0"
            }
        }

        MenuTextRow {
            text: qsTr("Start Radio")
        }

        MenuTextRow {
            text: qsTr("Save to Your Music")
        }

        MenuTextRow {
            text: qsTr("Add to %1 Playlist").arg(menu.musicServiceName)
        }

        MenuTextRow {
            text: qsTr("Album Info")
        }

        MenuTextRow {
            text: qsTr("Browse Artist")
        }
    }

    component MenuTextRow: Item {
        id: row

        property string text: ""
        signal clicked()

        Layout.fillWidth: true
        implicitHeight: 32

        Rectangle {
            anchors.fill: parent
            radius: 6
            color: rowMouseArea.pressed
                ? "#D0D0D0"
                : (rowMouseArea.containsMouse ? "#F0F0F0" : "transparent")
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: row.text
            font.pixelSize: 13
            color: "#212121"
            elide: Text.ElideRight
        }

        MouseArea {
            id: rowMouseArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: row.clicked()
        }
    }
}
