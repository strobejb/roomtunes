import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

// Small popup-based context menu, styled to match Panel.qml's white
// rounded-corner-plus-shadow look rather than relying on the platform's
// default QQC2 Menu appearance (plain/inconsistent across styles). Not a
// generic Menu replacement -- just enough for short, static action lists
// like the settings buttons' menus.
//
// `items` is a flat list of strings; the sentinel "-" renders as a
// separator rather than a clickable row. Positioning (x/y relative to the
// button that opened it) is left to the caller, since it depends on which
// corner of the panel the triggering button sits in.
Popup {
    id: menu

    property var items: []

    signal itemClicked(string text)

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 6

    // The window-wide dim (see Main.qml's own overlayDim Rectangle) is
    // hand-rolled and animated there instead of relying on this Popup's
    // own default modal-dimming visual -- suppress that one entirely so
    // the two don't double up/flash against each other, while keeping
    // modal:true itself for the outside-press-to-close and focus-trap
    // behavior it still provides.
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

        Repeater {
            model: menu.items

            delegate: Item {
                id: row
                Layout.fillWidth: true
                implicitWidth: modelData === "-" ? 1 : (rowText.implicitWidth + 28)
                implicitHeight: modelData === "-" ? 9 : 32

                Rectangle {
                    visible: modelData === "-"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: 1
                    color: "#E0E0E0"
                }

                Rectangle {
                    visible: modelData !== "-"
                    anchors.fill: parent
                    radius: 6
                    color: rowMouseArea.pressed
                        ? "#D0D0D0"
                        : (rowMouseArea.containsMouse ? "#F0F0F0" : "transparent")
                }

                Text {
                    id: rowText
                    visible: modelData !== "-"
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData
                    font.pixelSize: 13
                    color: "#212121"
                }

                MouseArea {
                    id: rowMouseArea
                    anchors.fill: parent
                    enabled: modelData !== "-"
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        menu.itemClicked(modelData)
                        menu.close()
                    }
                }
            }
        }
    }
}
