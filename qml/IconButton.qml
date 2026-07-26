import QtQuick

// Small reusable circular-hover icon button for header/corner affordances
// (pause-all, settings) that sit directly on a plain white Panel, alongside
// the existing SearchIconButton -- generic over iconSource since these
// appear in more than one place with different icons, unlike
// SearchIconButton's single hardcoded use.
Item {
    id: button

    property url iconSource: ""
    property int iconSize: 18
    // Default reproduces the plain "transparent until hovered" affordance
    // used elsewhere (settings/pause icons); callers that need a
    // persistently-visible background (e.g. the Queue panel's toolbar,
    // which wants contrast against the panel even at rest) override both.
    property color idleColor: "transparent"
    property color hoverColor: "#F0F0F0"
    property color pressedColor: "#D0D0D0"
    readonly property bool hovered: mouseArea.containsMouse

    signal clicked()
    signal pressed()

    implicitWidth: 32
    implicitHeight: 32

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: mouseArea.pressed
            ? button.pressedColor
            : (mouseArea.containsMouse ? button.hoverColor : button.idleColor)
    }

    Image {
        anchors.centerIn: parent
        source: button.iconSource
        sourceSize.width: button.iconSize
        sourceSize.height: button.iconSize
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: button.clicked()
        onPressed: button.pressed()
    }
}
