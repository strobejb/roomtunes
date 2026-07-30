import QtQuick
import QtQuick.Layouts

// Reusable pill-shaped icon+label button (e.g. the Queue panel's Edit
// button) -- pairs with IconButton (icon-only) and SearchIconButton for the
// same "transparent until hovered, light grey fill on hover" affordance
// used throughout the app.
Item {
    id: button

    property url iconSource: ""
    property int iconSize: 16
    property string label: ""
    property int fontPixelSize: 14
    property color textColor: "#212121"
    // See IconButton.qml -- same override pattern for callers that want a
    // persistently-visible background rather than transparent-until-hover.
    property color idleColor: "transparent"
    property color hoverColor: "#F0F0F0"
    property color pressedColor: "#D0D0D0"

    signal clicked()

    implicitWidth: content.implicitWidth + 24
    implicitHeight: 32

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        antialiasing: true
        color: mouseArea.pressed
            ? button.pressedColor
            : (mouseArea.containsMouse ? button.hoverColor : button.idleColor)
    }

    RowLayout {
        id: content
        anchors.centerIn: parent
        spacing: 6

        Image {
            visible: !!button.iconSource.toString()
            source: button.iconSource
            sourceSize.width: button.iconSize
            sourceSize.height: button.iconSize
            smooth: true
        }

        Text {
            text: button.label
            font.pixelSize: button.fontPixelSize
            color: button.textColor
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: button.clicked()
    }
}
