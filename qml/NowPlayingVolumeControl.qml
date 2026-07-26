import QtQuick

// Corner volume icon that reveals a vertical slider growing downward from
// it, either on hover or after click depending on expandOnHover. Positioned
// by NowPlayingPanel.qml (top-right corner of
// the panel) as a sibling of whichever layout (NowPlayingWide.qml/
// NowPlayingCompact.qml) is currently active, so it stays in a fixed
// place regardless of which one is showing. In hover mode, clicking the
// icon toggles mute; in click-to-open mode, the first click opens the pill.
// Sonos itself leaves CurrentVolume untouched while muted -- the slider
// display forces its own fill to 0 while muted regardless (see
// volumeSlider.liveRatio below), and un-muting "restores" it for free
// since the real, never-touched value is just read again.
Item {
    id: root

    property var zone // ZonePlayer* -- null when no zone is selected
    property bool backgroundIsLight: true
    property color contrastColor: "#212121"
    property color controlHoverColor: Qt.rgba(0, 0, 0, 0.1)
    property color controlPressedColor: Qt.rgba(0, 0, 0, 0.2)
    property bool expandOnHover: true
    function blendToward(base, target, amount) {
        return Qt.rgba(
            base.r + (target.r - base.r) * amount,
            base.g + (target.g - base.g) * amount,
            base.b + (target.b - base.b) * amount,
            1.0)
    }

    // volume_x (crossed-out glyph) when actually muted; otherwise volume_0
    // (no waves) below 5%, volume_1 (one wave) below 20%, volume_2 (two
    // waves -- the loudest glyph this icon set has) at 20%+. Thresholds
    // ported from roomtunes-bb10's Sonosjs.volumeIcon()/volumeSuffix()
    // (5/20/50%, mapping to four icon tiers 0/1/2/3) -- collapsed to this
    // app's three-tier icon set (no volume_3 asset exists) by merging
    // bb10's own top two tiers into one "loud" glyph.
    readonly property string volumeIconName: {
        if (!zone)
            return "volume_1"
        if (zone.muted)
            return "volume_x"
        if (zone.volume < 5)
            return "volume_0"
        if (zone.volume < 20)
            return "volume_1"
        return "volume_2"
    }

    width: 44
    // Fixed at the full max footprint always -- not animated. See
    // volumeMouseArea below for why this has to be a single MouseArea
    // rather than several stacked ones.
    height: 44 + sliderLength
    visible: root.zone !== null

    // 134, not 110 -- grown to fit the volume-level label below the
    // slider (see volumeLabel below) while keeping the slider's own
    // draggable track (sliderHeight) the same length it always was.
    readonly property int sliderLength: 134
    readonly property int iconHeight: 44
    readonly property int sliderTop: iconHeight + 4
    readonly property int sliderHeight: 90
    property bool opened: false
    readonly property bool expanded: opened || volumeMouseArea.hoverOpened || volumeSlider.dragging

    // The one thing that actually animates -- purely cosmetic, doesn't
    // feed back into `expanded`.
    Rectangle {
        id: pillBackground
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: 44
        height: root.expanded ? 44 + root.sliderLength : 44
        radius: width / 2
        color: root.expanded
            ? (volumeSlider.dragging ? root.controlPressedColor : root.controlHoverColor)
            : (volumeMouseArea.iconHovered ? root.controlHoverColor : "transparent")

        Behavior on height {
            NumberAnimation { duration: 150; easing.type: Easing.OutQuad }
        }
    }

    Image {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: (root.iconHeight - height) / 2
        source: root.backgroundIsLight
            ? "../resources/icons/" + root.volumeIconName + ".svg"
            : "../resources/icons/" + root.volumeIconName + "_light.svg"
        sourceSize.width: 26
        sourceSize.height: 26
    }

    Item {
        id: volumeSlider
        anchors.top: parent.top
        anchors.topMargin: root.sliderTop
        anchors.horizontalCenter: parent.horizontalCenter
        width: 12
        height: root.sliderHeight
        visible: root.expanded

        // Reads as 0 while muted rather than the real (unchanged) device
        // volume -- see the class comment above.
        readonly property real liveRatio:
            (root.zone && !root.zone.muted) ? Math.max(0, Math.min(1, root.zone.volume / 100)) : 0
        property bool dragging: false
        property real dragRatio: 0
        readonly property real displayRatio: dragging ? dragRatio : liveRatio

        Rectangle {
            id: volumeTrack
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            width: 3
            radius: 1.5
            color: root.contrastColor
            opacity: 0.3
        }

        // Fills from the bottom upward as volume increases, same
        // convention as a physical volume slider.
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            width: 3
            radius: 1.5
            height: volumeTrack.height * volumeSlider.displayRatio
            color: root.contrastColor
        }

        Rectangle {
            width: 10
            height: 10
            radius: 5
            color: root.contrastColor
            anchors.horizontalCenter: parent.horizontalCenter
            y: volumeTrack.height * (1 - volumeSlider.displayRatio) - height / 2
        }
    }

    // Balances the icon at the top -- mirrors volumeSlider's own
    // displayRatio (not root.zone.volume directly) so it always agrees
    // with the fill/thumb position: the live dragged value while
    // dragging, and 0 while muted, same reasoning as the fill Rectangle
    // above.
    Text {
        anchors.top: volumeSlider.bottom
        anchors.topMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        visible: root.expanded
        text: root.zone ? Math.round(volumeSlider.displayRatio * 100) : ""
        font.pixelSize: 14
        color: root.contrastColor
        opacity: 0.85
    }

    // ONE MouseArea for the whole pill (icon + slider), covering the full
    // static footprint -- not several stacked ones. Stacked MouseAreas
    // were the actual bug: volumeIconArea and a separate slider MouseArea
    // sat on top of a full-footprint hover-tracking MouseArea underneath,
    // and between them covered nearly all of its surface. Whenever the
    // cursor was over the icon or slider (i.e. almost anywhere in the
    // pill), the item underneath lost containsMouse -- confirmed
    // directly: logging showed the underneath area's containsMouse
    // flipping continuously even though its own bounds never changed,
    // purely from being obscured by the areas on top of it. Since
    // `expanded` read that flickering value, the whole pill was rapidly
    // collapsing and re-expanding the entire time the mouse sat over it.
    // A single MouseArea has nothing to be obscured by, so nothing to
    // flicker.
    MouseArea {
        id: volumeMouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor

        // Which region the button went down in -- decides whether
        // onClicked toggles mute (icon) or is a no-op (slider, where the
        // drag itself already applied the new volume on release).
        property bool pressStartedInIcon: false

        property bool iconHovered: containsMouse && mouseY <= root.iconHeight
        property bool hoverOpened: false

        function ratioFor(mouseY) {
            return 1 - Math.max(0, Math.min(1, (mouseY - root.sliderTop) / root.sliderHeight))
        }

        onContainsMouseChanged: {
            if (!containsMouse) {
                root.opened = false
                hoverOpened = false
            } else if (root.expandOnHover && mouseY <= root.iconHeight) {
                hoverOpened = true
            }
        }

        onPressed: mouse => {
            pressStartedInIcon = mouse.y <= root.iconHeight
            if (root.expanded && !pressStartedInIcon) {
                volumeSlider.dragging = true
                volumeSlider.dragRatio = ratioFor(mouse.y)
            }
        }
        onPositionChanged: mouse => {
            if (containsMouse && root.expandOnHover && !hoverOpened && mouse.y <= root.iconHeight)
                hoverOpened = true
            if (volumeSlider.dragging)
                volumeSlider.dragRatio = ratioFor(mouse.y)
        }
        onReleased: mouse => {
            if (volumeSlider.dragging) {
                volumeSlider.dragRatio = ratioFor(mouse.y)
                volumeSlider.dragging = false
                root.zone.setVolume(Math.round(volumeSlider.dragRatio * 100))
            }
        }
        onClicked: {
            if (pressStartedInIcon) {
                if (root.expandOnHover || root.expanded)
                    root.zone.setMuted(!root.zone.muted)
                else
                    root.opened = true
            }
        }
    }
}
