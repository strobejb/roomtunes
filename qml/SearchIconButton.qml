import QtQuick

// Search icon button for the Browse panel header. Not wired to any search
// functionality yet -- a visual affordance only, same as the title bar's
// menu button and each service row's chevron.
Item {
    id: button

    signal clicked()

    implicitWidth: 32
    implicitHeight: 32

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: mouseArea.containsMouse ? "#F0F0F0" : "transparent"
    }

    Image {
        anchors.centerIn: parent
        source: "../resources/icons/search.svg"
        sourceSize.width: 18
        sourceSize.height: 18
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: button.clicked()
    }
}
