import QtQuick

// 8 edge/corner drag-resize handles for the frameless window. Needed on
// every platform, including Windows: collapsing the non-client area (see
// WindowsChrome.cpp) also disables the OS's automatic edge-resize
// hit-testing, so it has to be done by hand here instead.
//
// Corners are declared after the edges so they win the small overlapping
// region at each corner (later-declared siblings at the same z stack on
// top and receive input first).
Item {
    id: root

    property var appWindow
    property int edgeSize: 6
    property int cornerSize: 10

    anchors.fill: parent

    MouseArea {
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: root.edgeSize
        cursorShape: Qt.SizeVerCursor
        onPressed: root.appWindow && root.appWindow.startSystemResize(Qt.TopEdge)
    }
    MouseArea {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: root.edgeSize
        cursorShape: Qt.SizeVerCursor
        onPressed: root.appWindow && root.appWindow.startSystemResize(Qt.BottomEdge)
    }
    MouseArea {
        anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
        width: root.edgeSize
        cursorShape: Qt.SizeHorCursor
        onPressed: root.appWindow && root.appWindow.startSystemResize(Qt.LeftEdge)
    }
    MouseArea {
        anchors { top: parent.top; bottom: parent.bottom; right: parent.right }
        width: root.edgeSize
        cursorShape: Qt.SizeHorCursor
        onPressed: root.appWindow && root.appWindow.startSystemResize(Qt.RightEdge)
    }

    MouseArea {
        anchors { top: parent.top; left: parent.left }
        width: root.cornerSize
        height: root.cornerSize
        cursorShape: Qt.SizeFDiagCursor
        onPressed: root.appWindow && root.appWindow.startSystemResize(Qt.TopEdge | Qt.LeftEdge)
    }
    MouseArea {
        anchors { top: parent.top; right: parent.right }
        width: root.cornerSize
        height: root.cornerSize
        cursorShape: Qt.SizeBDiagCursor
        onPressed: root.appWindow && root.appWindow.startSystemResize(Qt.TopEdge | Qt.RightEdge)
    }
    MouseArea {
        anchors { bottom: parent.bottom; left: parent.left }
        width: root.cornerSize
        height: root.cornerSize
        cursorShape: Qt.SizeBDiagCursor
        onPressed: root.appWindow && root.appWindow.startSystemResize(Qt.BottomEdge | Qt.LeftEdge)
    }
    MouseArea {
        anchors { bottom: parent.bottom; right: parent.right }
        width: root.cornerSize
        height: root.cornerSize
        cursorShape: Qt.SizeFDiagCursor
        onPressed: root.appWindow && root.appWindow.startSystemResize(Qt.BottomEdge | Qt.RightEdge)
    }
}
