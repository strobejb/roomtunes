import QtQuick
import QtQuick.Controls

// Subtle pill-shaped scroll-position indicator for the Queue/Browse/search
// results lists -- attach as a ListView's ScrollBar.vertical, and give
// that ListView's own delegates a little less than the full list width
// (see callers) so the thumb has real breathing room from the row content
// instead of touching it. Still a real (draggable) QQC2 ScrollBar
// underneath, just restyled down to a slim rounded bar -- no visible
// track/gutter behind it, light grey normally, darker on hover/press.
ScrollBar {
    id: control

    orientation: Qt.Vertical
    policy: ScrollBar.AsNeeded
    implicitWidth: 7

    // The framework computes size/position correctly on its own (size==1.0
    // whenever the list already fits, no scrolling possible), but visibility
    // itself is purely a style concern -- Qt's own Basic style's ScrollBar.qml
    // hides both the control and its thumb once size reaches 1.0, and since
    // this component replaces that style's contentItem/background wholesale,
    // it has to replace that visibility rule too or the thumb never hides.
    visible: control.size < 1.0

    // Attaching via "ScrollBar.vertical: SlimScrollBar {}" auto-docks this
    // control flush against its Flickable's own right edge (top/right/bottom
    // anchored internally) -- these margins just pull it in a few px from
    // that edge so it isn't touching the list's true boundary.
    anchors.topMargin: 2
    anchors.bottomMargin: 2
    anchors.rightMargin: 2

    contentItem: Rectangle {
        implicitWidth: 7
        radius: width / 2
        color: control.pressed ? "#9E9E9E" : (control.hovered ? "#BDBDBD" : "#D6D6D6")
        visible: control.size < 1.0

        Behavior on color {
            ColorAnimation { duration: 120 }
        }
    }

    background: Item {}
}
