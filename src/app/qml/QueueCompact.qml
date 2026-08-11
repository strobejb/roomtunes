import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Compact placeholder for the queue while the narrow zones list is expanded.
Item {
    id: root

    signal expandRequested()
    implicitHeight: 44

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.expandRequested()
    }

    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        height: 32
        spacing: 8

        Label {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            text: qsTr("Queue")
            font.pixelSize: Math.round(18 * UiScale.factor)
            font.weight: Typography.emphasisWeight
            color: "#212121"
        }

        IconButton {
            iconSource: "../resources/icons/triangle_up.svg"
            iconSize: 10
            idleColor: "transparent"
            hoverColor: "#E8E8E8"
            pressedColor: "#D0D0D0"
            onClicked: root.expandRequested()
        }
    }
}
