import QtQuick

// Minimalist scrub bar: a thin line the width of this item with a small
// draggable thumb, plus elapsed/remaining time labels below it -- shared
// between NowPlayingWide.qml and NowPlayingCompact.qml. Caller sizes it
// via width (or Layout.fillWidth); implicitHeight covers the track plus
// the time-label row beneath it.
Item {
    id: root

    property var zone // ZonePlayer* -- null when no zone is selected
    property color contrastColor: "#212121"
    // True while NowPlayingPanel.qml's volume pill is expanded -- the
    // scrub bar's own content hides rather than being fought over the
    // same screen space in NowPlayingWide.qml's layout (where the volume
    // pill's corner overlay and this bar's right edge can overlap).
    // NowPlayingCompact.qml can just leave this false if its own
    // arrangement never overlaps the pill.
    property bool hideForVolume: false

    implicitHeight: 30

    readonly property int durationSeconds: root.zone ? root.zone.durationSeconds : 0
    readonly property int positionSeconds: root.zone ? root.zone.positionSeconds : 0
    readonly property real liveRatio:
        durationSeconds > 0 ? Math.max(0, Math.min(1, positionSeconds / durationSeconds)) : 0

    // While the user is actively dragging the thumb, show where they've
    // dragged to rather than fighting it with whatever the last poll
    // reported.
    property bool dragging: false
    property real dragRatio: 0
    readonly property real displayRatio: dragging ? dragRatio : liveRatio
    readonly property int displaySeconds:
        dragging ? Math.round(dragRatio * durationSeconds) : positionSeconds

    function formatPosition(totalSeconds) {
        var m = Math.floor(totalSeconds / 60)
        var s = totalSeconds % 60
        return m + ":" + (s < 10 ? "0" : "") + s
    }

    Item {
        id: scrubBarContent
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 12
        opacity: root.hideForVolume ? 0 : 1
        enabled: !root.hideForVolume

        Rectangle {
            id: scrubTrack
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: 3
            radius: 1.5
            color: root.contrastColor
            opacity: 0.3
        }

        Rectangle {
            // Played portion ends at the thumb's *center*, not the raw
            // ratio-scaled track width, so it still lines up once the
            // thumb itself is inset (see scrubThumb below).
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            height: 3
            radius: 1.5
            width: scrubThumb.width / 2
                   + (scrubTrack.width - scrubThumb.width) * root.displayRatio
            color: root.contrastColor
        }

        Rectangle {
            id: scrubThumb
            width: 10
            height: 10
            radius: 5
            color: root.contrastColor
            anchors.verticalCenter: parent.verticalCenter
            // Inset by half the thumb's own width at each end, so it
            // stays fully within scrubTrack's bounds at ratio 0 and 1
            // instead of hanging half off the ends -- displayRatio still
            // maps linearly across the whole 0-1 range.
            x: (scrubTrack.width - width) * root.displayRatio
        }

        MouseArea {
            id: scrubMouseArea
            anchors.fill: parent
            enabled: root.durationSeconds > 0
            cursorShape: Qt.PointingHandCursor

            function ratioFor(mouseX) {
                return Math.max(0, Math.min(1, mouseX / width))
            }

            onPressed: mouse => {
                root.dragging = true
                root.dragRatio = ratioFor(mouse.x)
            }
            onPositionChanged: mouse => {
                if (root.dragging)
                    root.dragRatio = ratioFor(mouse.x)
            }
            onReleased: mouse => {
                root.dragRatio = ratioFor(mouse.x)
                root.dragging = false
                root.zone.seek(root.dragRatio)
            }
        }
    }

    Text {
        anchors.top: scrubBarContent.bottom
        anchors.topMargin: 4
        anchors.left: parent.left
        text: root.formatPosition(root.displaySeconds)
        font.pixelSize: 11
        color: root.contrastColor
        opacity: root.hideForVolume ? 0 : 0.55
    }

    Text {
        anchors.top: scrubBarContent.bottom
        anchors.topMargin: 4
        anchors.right: parent.right
        text: root.formatPosition(root.durationSeconds)
        font.pixelSize: 11
        color: root.contrastColor
        opacity: root.hideForVolume ? 0 : 0.55
    }
}
