import QtQuick

// Small reusable search glyph button. BrowseHome/BrowseListPage wrap their
// own expanding search pills around the same visual language when they need a
// full text field.
Item {
    id: button

    signal clicked()

    implicitWidth: 32
    implicitHeight: 32

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        antialiasing: true
        color: mouseArea.containsMouse ? "#F0F0F0" : "transparent"
    }

    Image {
        anchors.centerIn: parent
        source: "../resources/icons/search.svg"
        sourceSize.width: 18
        sourceSize.height: 18
        smooth: true
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: button.clicked()
    }
}
