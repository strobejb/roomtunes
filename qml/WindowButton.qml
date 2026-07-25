import QtQuick
import QtQuick.Layouts

// One caption button (minimize / maximize-restore / close) for the custom
// title bar. Same reusable-glyph-button idea as TransportIconButton.qml,
// but sized/shaped like a native Windows/GNOME caption button (wide
// rectangle, not a circle) and with the close button's red hover state.
Item {
    id: button

    property string glyph: ""
    property bool danger: false // close button: red hover instead of grey

    signal clicked()

    implicitWidth: 46
    implicitHeight: 32
    // Explicit, not just relying on the implicitWidth/Height fallback --
    // as a Repeater delegate nested two RowLayouts deep, this button was
    // observed getting its size from the fallback but never an arranged
    // position (every instance sat at (0,0)) until this was made explicit.
    Layout.preferredWidth: implicitWidth
    Layout.preferredHeight: implicitHeight

    Rectangle {
        anchors.fill: parent
        color: {
            if (!mouseArea.containsMouse)
                return "transparent"
            if (button.danger)
                return mouseArea.pressed ? "#C42B1C" : "#E81123"
            return mouseArea.pressed ? "#D6D6D6" : "#14000000"
        }
    }

    Text {
        anchors.centerIn: parent
        text: button.glyph
        font.pixelSize: 11
        color: button.danger && mouseArea.containsMouse ? "white" : "#3A3A3A"
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.ArrowCursor
        onClicked: button.clicked()
    }

}
