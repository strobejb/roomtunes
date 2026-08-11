import QtQuick
import QtQuick.Layouts
import QtQuick.Window

// One caption button (minimize / maximize-restore / close) for the custom
// title bar. Same reusable-glyph-button idea as TransportIconButton.qml:
// Windows keeps wide rectangular caption buttons, while GNOME/Linux uses
// compact circular hover buttons.
Item {
    id: button

    property string glyph: ""
    property string buttonName: ""
    property bool danger: false // close button: red hover instead of grey
    property bool linuxHeaderBar: false
    property var appWindow: null
    property int glyphPixelSize: linuxHeaderBar ? 18 : 11

    readonly property bool useSymbolicIcon: linuxHeaderBar && buttonName.length > 0
    readonly property url symbolicIconSource: {
        if (!useSymbolicIcon)
            return ""
        if (buttonName === "close")
            return "../resources/icons/window_close_symbolic.svg"
        if (buttonName === "minimize")
            return "../resources/icons/window_minimize_symbolic.svg"
        if (buttonName === "maximize" && appWindow && appWindow.visibility === Window.Maximized)
            return "../resources/icons/window_restore_symbolic.svg"
        if (buttonName === "maximize")
            return "../resources/icons/window_maximize_symbolic.svg"
        return ""
    }

    signal clicked()

    implicitWidth: linuxHeaderBar ? 34 : 46
    implicitHeight: linuxHeaderBar ? 34 : 32
    // Explicit, not just relying on the implicitWidth/Height fallback --
    // as a Repeater delegate nested two RowLayouts deep, this button was
    // observed getting its size from the fallback but never an arranged
    // position (every instance sat at (0,0)) until this was made explicit.
    Layout.preferredWidth: implicitWidth
    Layout.preferredHeight: implicitHeight

    Rectangle {
        anchors.fill: parent
        radius: button.linuxHeaderBar ? width / 2 : 0
        antialiasing: button.linuxHeaderBar
        color: {
            if (!mouseArea.containsMouse)
                return "transparent"
            if (button.danger)
                return button.linuxHeaderBar ? (mouseArea.pressed ? "#26000000" : "#14000000")
                                             : (mouseArea.pressed ? "#C42B1C" : "#E81123")
            return mouseArea.pressed ? (button.linuxHeaderBar ? "#26000000" : "#D6D6D6") : "#14000000"
        }
    }

    Image {
        anchors.centerIn: parent
        visible: button.useSymbolicIcon
        source: button.symbolicIconSource
        sourceSize.width: 16
        sourceSize.height: 16
        smooth: true
    }

    Text {
        anchors.centerIn: parent
        visible: !button.useSymbolicIcon
        text: button.glyph
        font.pixelSize: button.glyphPixelSize
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
