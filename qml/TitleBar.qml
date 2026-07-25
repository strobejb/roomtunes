import QtQuick
import QtQuick.Window
import QtQuick.Layouts

// Custom-drawn title bar: draggable title strip + platform-ordered
// minimize/maximize/close buttons. Button glyphs and left/right placement
// come from PlatformChrome (fixed on Windows, read from GNOME's own
// gsettings button-layout on Linux) so the custom bar matches whatever
// native windows already look like on that desktop.
Item {
    id: titleBar

    property var appWindow // the ApplicationWindow this bar controls
    property string titleText: appWindow ? appWindow.title : ""
    // X (in this item's own coordinates) to center the title text over.
    // Defaults to the title bar's own center; Main.qml overrides this to
    // the Now Playing column's center instead.
    property real centerX: width / 2

    implicitHeight: 40

    function glyphFor(name) {
        switch (name) {
        case "minimize": return "─"
        case "maximize": return appWindow && appWindow.visibility === Window.Maximized ? "❑" : "□"
        case "close": return "✕"
        default: return ""
        }
    }

    function toggleMaximize() {
        if (!appWindow) return
        appWindow.visibility = (appWindow.visibility === Window.Maximized) ? Window.Windowed : Window.Maximized
    }

    function activate(name) {
        if (!appWindow) return
        switch (name) {
        case "minimize": appWindow.showMinimized(); break
        case "maximize": toggleMaximize(); break
        case "close": appWindow.close(); break
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // App menu button -- purely for visual balance against the window
        // controls on the right for now; not wired to a menu yet.
        WindowButton {
            id: menuButton
            glyph: "☰"
        }

        // Repeater delegates directly as RowLayout children (not wrapped in
        // their own nested RowLayout) -- a RowLayout-within-RowLayout
        // wrapper here doesn't reliably pick up its implicit size from
        // Repeater-instantiated children, leaving every button stacked at
        // (0,0) instead of laid out side by side.
        Repeater {
            model: PlatformChrome.leftButtons
            delegate: WindowButton {
                glyph: titleBar.glyphFor(modelData)
                danger: modelData === "close"
                onClicked: titleBar.activate(modelData)
            }
        }

        MouseArea {
            id: dragArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            acceptedButtons: Qt.LeftButton

            onPressed: (mouse) => {
                if (mouse.button === Qt.LeftButton && titleBar.appWindow)
                    titleBar.appWindow.startSystemMove()
            }
            onDoubleClicked: titleBar.toggleMaximize()
        }

        Repeater {
            model: PlatformChrome.rightButtons
            delegate: WindowButton {
                glyph: titleBar.glyphFor(modelData)
                danger: modelData === "close"
                onClicked: titleBar.activate(modelData)
            }
        }
    }

    // Centered on titleBar.centerX, not just on dragArea's leftover space
    // or the title bar's own midpoint -- dragArea only spans what's left
    // after the button groups, and the title bar's own midpoint isn't the
    // same as the Now Playing column's midpoint when the side columns are
    // different widths. A plain Text with no MouseArea doesn't intercept
    // drag/click input, so it can sit on top of the RowLayout unattached.
    Text {
        x: titleBar.centerX - width / 2
        anchors.verticalCenter: parent.verticalCenter
        text: titleBar.titleText
        font.pixelSize: 13
        font.bold: true
        color: "#3A3A3A"
        elide: Text.ElideRight
    }
}
