import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

Popup {
    id: dialog

    signal saveRequested(string title)

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: 340
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 20

    function openFresh() {
        playlistNameField.text = ""
        open()
    }

    onOpened: playlistNameField.forceActiveFocus()

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
        spacing: 14

        Label {
            Layout.fillWidth: true
            text: qsTr("New Playlist")
            font.pixelSize: 16
            font.weight: Typography.emphasisWeight
            color: "#212121"
        }

        TextField {
            id: playlistNameField
            Layout.fillWidth: true
            placeholderText: qsTr("Playlist name")
            selectByMouse: true
            font.pixelSize: 14
            onAccepted: {
                const title = text.trim()
                if (title.length === 0)
                    return
                dialog.saveRequested(title)
                dialog.close()
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 10

            Item {
                id: cancelButton
                implicitWidth: cancelLabel.implicitWidth + 36
                implicitHeight: cancelLabel.implicitHeight + 16

                Rectangle {
                    anchors.fill: parent
                    radius: height / 2
                    color: cancelMouseArea.pressed ? "#D0D0D0"
                                                   : (cancelMouseArea.containsMouse ? "#E8E8E8" : "#F0F0F0")
                }

                Text {
                    id: cancelLabel
                    anchors.centerIn: parent
                    text: qsTr("Cancel")
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: "#212121"
                }

                MouseArea {
                    id: cancelMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: dialog.close()
                }
            }

            Item {
                id: saveButton
                readonly property bool canSave: playlistNameField.text.trim().length > 0
                implicitWidth: saveLabel.implicitWidth + 40
                implicitHeight: saveLabel.implicitHeight + 16
                opacity: canSave ? 1.0 : 0.45

                Rectangle {
                    anchors.fill: parent
                    radius: height / 2
                    color: saveMouseArea.pressed ? "#D0D0D0"
                                                 : (saveMouseArea.containsMouse ? "#E8E8E8" : "#F0F0F0")
                }

                Text {
                    id: saveLabel
                    anchors.centerIn: parent
                    text: qsTr("Save")
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: "#212121"
                }

                MouseArea {
                    id: saveMouseArea
                    anchors.fill: parent
                    enabled: saveButton.canSave
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        dialog.saveRequested(playlistNameField.text.trim())
                        dialog.close()
                    }
                }
            }
        }
    }
}
