import QtQuick

// Same interaction model as NowPlayingVolumeControl.qml (hover highlights
// the icon circle, click reveals a draggable slider), but
// horizontal and expanding to the *left* instead of vertical/downward --
// fits the zone card's own top-right corner, where the icon has to stay
// fixed in place with the room name to its left, unlike Now Playing's
// open space below.
//
// Unlike that version, this one's own hit area (not just the visible
// pill) actually grows/shrinks with `expanded`, rather than always
// sitting at its full max footprint. An always-max horizontal footprint
// here would silently overlap the room name text even while collapsed,
// which NowPlayingVolumeControl never has to worry about -- there's
// nothing else competing for input in the space it reserves.
Item {
    id: root

    property var zone // ZonePlayer* -- null when no coordinator yet
    property bool backgroundIsLight: true
    property color contrastColor: "#212121"
    property bool expandOnHover: true

    // Contrast-based, not a fixed black tint -- this sits on cards that
    // can go dark/accent-colored (see ZoneGroupCard.qml), unlike Now
    // Playing's own fixed-background context. Same 0.10/0.18 alphas as
    // that card's other icon buttons (volume/settings/unlink), for a
    // consistent hover/press feel across all of them.
    readonly property color hoverColor: Qt.rgba(contrastColor.r, contrastColor.g, contrastColor.b, 0.10)
    readonly property color pressedColor: Qt.rgba(contrastColor.r, contrastColor.g, contrastColor.b, 0.18)

    readonly property int displayVolume: slider.dragging
        ? Math.round(slider.dragRatio * 100)
        : ((zone && zone.volumeKnown) ? zone.volume : 20)
    readonly property bool displayMuted: slider.dragging
        ? displayVolume <= 0
        : !!zone && zone.muteKnown && zone.muted
    readonly property string volumeIconName: VolumeIcon.nameFor(
        displayVolume, displayMuted)
    readonly property bool stateKnown: !!zone && zone.volumeKnown && zone.muteKnown

    readonly property int iconSize: 32
    readonly property int sliderLength: 90
    readonly property int sliderGap: 10
    readonly property int labelGap: 8
    readonly property int labelWidth: 26
    readonly property int expandedWidth: iconSize + sliderGap + sliderLength + labelGap + labelWidth

    height: iconSize
    width: expanded ? expandedWidth : iconSize
    visible: root.zone !== null

    property bool opened: false
    readonly property bool expanded: opened || mouseArea.hoverOpened || slider.dragging
    readonly property real expandedOpacity: expanded ? 1 : 0

    // Matches the card's own current background -- unlike
    // NowPlayingVolumeControl.qml (which expands into open space below
    // it), this expands leftward *over* the card's own room-name/status
    // text. Without an opaque backdrop here, that text shows straight
    // through the low-alpha hover tint underneath the slider/label,
    // reading as a garbled overlap (e.g. the status row's own "Vol 63"
    // running directly into this control's "63" label).
    property color pillColor: "white"
    readonly property color pressedOverlayColor: root.backgroundIsLight
        ? Qt.rgba(1, 1, 1, 0.22)
        : Qt.rgba(0, 0, 0, 0.22)

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: root.expanded ? root.pillColor : "transparent"
        opacity: root.expandedOpacity

        Behavior on opacity {
            NumberAnimation { duration: 150; easing.type: Easing.OutQuad }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: root.expanded
            ? (slider.dragging ? root.pressedOverlayColor : root.hoverColor)
            : (mouseArea.iconHovered ? root.hoverColor : "transparent")

        Behavior on color {
            ColorAnimation { duration: 120 }
        }
    }

    Item {
        id: iconSlot
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: root.iconSize

        Image {
            anchors.centerIn: parent
            source: root.backgroundIsLight
                ? "../resources/icons/" + root.volumeIconName + ".svg"
                : "../resources/icons/" + root.volumeIconName + "_light.svg"
            sourceSize.width: 18
            sourceSize.height: 18
            opacity: root.stateKnown ? 1.0 : 0.45
        }
    }

    Item {
        id: slider
        anchors.right: parent.right
        anchors.rightMargin: root.iconSize + root.sliderGap
        anchors.verticalCenter: parent.verticalCenter
        width: root.sliderLength
        height: 12
        visible: root.expanded
        opacity: root.expandedOpacity

        Behavior on opacity {
            NumberAnimation { duration: 150; easing.type: Easing.OutQuad }
        }

        // Reads as 0 while muted rather than the real (unchanged) device
        // volume -- same reasoning as NowPlayingVolumeControl.qml's own
        // liveRatio.
        readonly property real liveRatio:
            !root.zone ? 0
                : (root.zone.muteKnown && root.zone.muted) ? 0
                : Math.max(0, Math.min(1, root.displayVolume / 100))
        property bool dragging: false
        property real dragRatio: 0
        readonly property real displayRatio: dragging ? dragRatio : liveRatio

        Rectangle {
            id: volumeTrack
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: 3
            radius: 1.5
            color: root.contrastColor
            opacity: 0.3
        }

        // Fills from the left, growing toward the icon on the right as
        // volume increases -- horizontal counterpart of
        // NowPlayingVolumeControl's own "fills from the bottom upward,
        // toward the icon" convention.
        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            height: 3
            radius: 1.5
            width: volumeTrack.width * slider.displayRatio
            color: root.contrastColor
        }

        Rectangle {
            width: 10
            height: 10
            radius: 5
            color: root.contrastColor
            anchors.verticalCenter: parent.verticalCenter
            x: volumeTrack.width * slider.displayRatio - width / 2
        }
    }

    // Balances the icon -- mirrors slider's own displayRatio (not
    // root.zone.volume directly), same reasoning as
    // NowPlayingVolumeControl.qml's own label.
    Text {
        anchors.right: slider.left
        anchors.rightMargin: root.labelGap
        anchors.verticalCenter: parent.verticalCenter
        visible: root.expanded
        text: root.zone ? Math.round(slider.displayRatio * 100) : ""
        font.pixelSize: 13
        color: root.contrastColor
        opacity: root.expandedOpacity * 0.85

        Behavior on opacity {
            NumberAnimation { duration: 150; easing.type: Easing.OutQuad }
        }
    }

    // Single MouseArea over root's own (now variable) footprint -- see
    // NowPlayingVolumeControl.qml's own comment for why one MouseArea
    // rather than several stacked ones. root.width changing underneath
    // it is fine: Qt Quick re-evaluates containsMouse against whatever
    // the current bounds are, so a press that starts the expansion keeps
    // being tracked correctly as the area grows around the still-hovered
    // point.
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        cursorShape: Qt.PointingHandCursor

        // Which region the button went down in -- decides whether onClicked
        // opens the pill/toggles mute (icon) or is a no-op (slider, where
        // the drag itself already applied the new volume on release).
        property bool pressStartedInIcon: false

        property bool iconHovered: containsMouse && inIcon(mouseX)
        property bool hoverOpened: false

        function inIcon(mouseX) {
            return mouseX >= root.width - root.iconSize
        }

        function ratioFor(mouseX) {
            // The slider's own left edge, in root's local coordinate
            // space, regardless of root's current (possibly
            // mid-animation) width.
            var sliderLeft = root.width - root.iconSize - root.sliderGap - root.sliderLength
            return Math.max(0, Math.min(1, (mouseX - sliderLeft) / root.sliderLength))
        }

        function finishSliderDrag(mouseX) {
            if (!slider.dragging)
                return
            slider.dragRatio = ratioFor(mouseX)
            slider.dragging = false
            if (root.zone) {
                var level = Math.round(slider.dragRatio * 100)
                if (level <= 0) {
                    root.zone.setMuted(true)
                } else {
                    if (root.zone.muted)
                        root.zone.setMuted(false)
                    root.zone.setVolume(level)
                }
            }
            if (!containsMouse) {
                root.opened = false
                hoverOpened = false
            }
        }

        onContainsMouseChanged: {
            if (!containsMouse) {
                root.opened = false
                hoverOpened = false
            } else if (root.expandOnHover && inIcon(mouseX)) {
                hoverOpened = true
            }
        }

        onPressed: mouse => {
            pressStartedInIcon = inIcon(mouse.x)
            if (root.expanded && !pressStartedInIcon) {
                slider.dragging = true
                slider.dragRatio = ratioFor(mouse.x)
            }
        }
        onPositionChanged: mouse => {
            if (containsMouse && root.expandOnHover && !hoverOpened && inIcon(mouse.x))
                hoverOpened = true
            if (slider.dragging)
                slider.dragRatio = ratioFor(mouse.x)
        }
        onReleased: mouse => {
            finishSliderDrag(mouse.x)
        }
        onCanceled: {
            finishSliderDrag(mouseX)
            if (!containsMouse) {
                root.opened = false
                hoverOpened = false
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
