import QtQuick
import QtQuick.Controls
import QtQuick.Effects

// One row in the zone list -- one card per Sonos play-group. A lone zone
// renders as a plain single-line card; zones joined into a group are all
// listed in the same card, separated by a faint divider.
Rectangle {
    id: card

    property var coordinator
    property var members: []
    property bool selected: false

    // Set by Main.qml to a single Item shared by every ZoneGroupCard in
    // zoneListView (see its own declaration for why this is one
    // statically-placed shared item rather than each card owning and
    // dynamically reparenting its own -- the latter never actually
    // rendered in testing despite every property being provably
    // correct).
    property Item dragGhost: null

    // Emitted when the card itself (not the group icon) is clicked -- used
    // to select this zone/group as the one shown in the Now Playing panel.
    signal clicked()

    // Pressing and dragging anywhere on the card (other than the icon
    // buttons layered on top of it -- volume/settings/unlink) requests
    // joining this card's zone into whichever other card it's dropped
    // on. dropRequested fires on the *source* card (the one that was
    // dragged) once the mouse is released over a valid target, carrying
    // both coordinators -- Main.qml calls sourceCoordinator.joinGroup
    // (targetCoordinator).
    signal dropRequested(var sourceCoordinator, var targetCoordinator)

    // Global (scene-coordinate) drag position while this card's own
    // handle is being dragged -- Main.qml's zoneListView listens for
    // this to auto-scroll when the drag nears the list's top/bottom
    // edge, since the drop target might be scrolled off-screen.
    signal dragPositionChanged(point globalPos)
    signal dragEnded()

    // Emitted by a member row's own unlink icon (only shown for
    // multi-zone groups -- see the Repeater below) to request removing
    // that one zone from the group -- Main.qml calls zone.leaveGroup().
    signal unlinkRequested(var zone)

    // True while this card is the current drop target -- set from
    // outside (by zoneListView's hit-testing in Main.qml, not by this
    // card itself), since eligibility is based on where the mouse is,
    // not on where the (possibly invisible) drag ghost's own bounds
    // happen to overlap.
    property bool dropHighlighted: false

    // Selected card picks up the same accent color as the Now Playing
    // panel (the coordinator's currently-playing track), instead of a
    // plain selection outline -- same computation as NowPlayingPanel.qml.
    readonly property bool hasAccent: selected && !!coordinator && coordinator.accentColor.a > 0
    readonly property color cardBackground: hasAccent ? coordinator.accentColor : "white"
    readonly property bool backgroundIsLight: luminance(cardBackground) > 0.55
    readonly property color contrastColor: backgroundIsLight ? "#212121" : "white"

    function luminance(c) {
        return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b
    }

    // Dimmed while this card is the one being dragged -- it can't be
    // dropped on itself, so this reads as "lifted out" rather than a
    // valid target. Keyed off dragging rather than the ghost's own
    // visible (which also depends on the mouse being inside the list --
    // see dragGhost's own comment) since the source card should stay
    // dimmed for the whole gesture, even while the ghost itself is
    // hidden.
    readonly property bool isDragSource: dragGhost && dragGhost.dragging && dragGhost.sourceCoordinator === card.coordinator

    // Drives the hover-only corner/inline icons below. A HoverHandler
    // (not a MouseArea) since it only needs to observe hover, not
    // accept/consume events -- it can sit directly on the card without
    // stealing presses from the click MouseArea or the icon buttons
    // layered on top of it.
    readonly property bool cardHovered: cardHoverHandler.hovered
    property bool dragArmed: false
    property bool suppressNextClick: false

    HoverHandler {
        id: cardHoverHandler
    }

    radius: 12
    color: cardBackground
    opacity: isDragSource ? 0.5 : 1.0
    implicitHeight: contentColumn.implicitHeight + 28

    Behavior on color {
        ColorAnimation { duration: 400 }
    }

    Behavior on opacity {
        NumberAnimation { duration: 150 }
    }

    layer.enabled: true
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowColor: "#1A000000"
        shadowBlur: 0.4
        shadowVerticalOffset: 1
        autoPaddingEnabled: false
    }

    // Click-to-select and hold-then-drag-to-group share this one MouseArea.
    // A plain click still selects, while a normal press-and-move is left for
    // the ListView to steal as scrolling. Group dragging is only armed after
    // a short stationary hold, which keeps the list easy to flick.
    MouseArea {
        id: cardMouseArea
        anchors.fill: parent
        cursorShape: card.dragGhost && card.dragGhost.dragging && !card.dragGhost.insideList
                     ? Qt.ForbiddenCursor
                     : Qt.PointingHandCursor
        drag.target: card.dragArmed ? card.dragGhost : null
        drag.axis: Drag.YAxis
        pressAndHoldInterval: 220

        onClicked: {
            if (!card.suppressNextClick)
                card.clicked()
            card.suppressNextClick = false
        }

        onPressed: {
            card.dragArmed = false
            card.suppressNextClick = false
        }

        onPressAndHold: {
            card.prepareDrag()
            card.dragArmed = true
            card.suppressNextClick = true
            card.revealDrag()
        }

        onReleased: {
            if (card.dragGhost && card.dragGhost.dragging)
                card.endDrag()
            card.dragArmed = false
        }

        onCanceled: {
            card.dragArmed = false
            card.suppressNextClick = false
        }

        // Only a real drag (past Qt's own threshold) reveals the ghost --
        // a plain click's press/release happens too close together for
        // startDrag()'s dashed/dimmed state to ever actually render, but a
        // slow deliberate click-and-hold-without-moving would otherwise
        // flash it for no reason.
        drag.onActiveChanged: {
            if (cardMouseArea.drag.active)
                card.revealDrag()
            else {
                card.endDrag()
                card.dragArmed = false
            }
        }

        // The actual mouse position, not the ghost's -- drag.target
        // preserves whatever offset existed between the mouse and the
        // ghost at press time, so the two diverge once dragging starts.
        // Drop eligibility is about where the *mouse* is relative to
        // each card, so this is the feed Main.qml's
        // zoneListView.updateDropTarget() needs; it also doubles as the
        // live position auto-scroll already listens for.
        onPositionChanged: (mouse) => {
            if (!card.dragGhost.dragging)
                return
            var scenePos = cardMouseArea.mapToItem(card.dragGhost.parent, mouse.x, mouse.y)
            card.dragPositionChanged(scenePos)
        }
    }

    // The drop-eligibility indicator itself -- an outline only (matching
    // the drag ghost's own look), on top of everything else so it reads
    // clearly regardless of the card's own background color.
    Rectangle {
        anchors.fill: parent
        radius: card.radius
        color: "transparent"
        border.width: 2
        border.color: "#424242"
        visible: card.dropHighlighted
    }

    // Mute-toggle icon, top-right corner -- always shown, always at full
    // strength (unlike the settings cog below) since volume/mute state
    // is worth seeing at a glance rather than only on hover. Hovering it
    // reveals the same drag-to-set-volume slider as
    // NowPlayingVolumeControl.qml, just horizontal and expanding to the
    // left (into the card) instead of vertical/downward -- see
    // ZoneVolumeControl.qml's own comment for why it needs its own
    // component rather than reusing that one directly.
    ZoneVolumeControl {
        z: 10
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 8
        zone: card.coordinator
        backgroundIsLight: card.backgroundIsLight
        contrastColor: card.contrastColor
        pillColor: card.cardBackground
    }

    // Split out from cardMouseArea's handlers above (rather than left
    // inline) so the same drag sequence can be driven programmatically
    // -- useful for verifying this without dragging a real mouse, the
    // same reasoning as this session's established "call .clicked() from
    // a Timer" testing pattern for ordinary click handlers, just for a
    // press/move/release sequence instead of a single call.
    //
    // Split into prepareDrag() (position/size only, called on every
    // press) and revealDrag() (just flips dragging on, called only once
    // a real drag starts) rather than one combined function -- see
    // cardMouseArea's onPressed/drag.onActiveChanged comments above for
    // why those two need different triggers.
    function prepareDrag() {
        // Size and position *before* the drag itself starts moving the
        // ghost -- drag.target's first move is a delta from wherever
        // its x/y already are, so it needs to already be sitting in the
        // right place (aligned with this card, in the ghost's own fixed
        // parent's coordinate space) before that happens.
        card.dragGhost.width = card.width
        card.dragGhost.height = card.height
        card.dragGhost.sourceCoordinator = card.coordinator
        card.dragGhost.sourceItem = card
        card.dragGhost.targetCoordinator = null
        card.dragGhost.insideList = true
        card.dragGhost.parkedAtSource = false
        card.dragGhost.animateToSource = false
        var startPos = card.mapToItem(card.dragGhost.parent, 0, 0)
        card.dragGhost.sourceX = startPos.x
        card.dragGhost.sourceY = startPos.y
        card.dragGhost.x = startPos.x
        card.dragGhost.y = startPos.y
    }

    function revealDrag() {
        card.dragGhost.dragging = true
    }

    // Only used by the programmatic drag simulation now -- real mouse
    // moves go through cardMouseArea.onPositionChanged above instead.
    // Still funnels through the same dragPositionChanged signal, so it
    // exercises the same hit-testing path. Calls both halves itself
    // since the simulation has no separate "press" vs. "drag started"
    // steps of its own.
    function startDrag() {
        prepareDrag()
        revealDrag()
    }

    function updateDrag(scenePos) {
        if (!card.dragGhost.dragging)
            return
        card.dragGhost.y = scenePos.y - card.dragGhost.height / 2
        card.dragPositionChanged(scenePos)
    }

    function endDrag() {
        if (card.dragGhost.dragging) {
            if (card.dragGhost.targetCoordinator
                    && card.dragGhost.targetCoordinator !== card.coordinator) {
                card.dropRequested(card.coordinator, card.dragGhost.targetCoordinator)
            }
            card.dragGhost.dragging = false
            card.dragGhost.targetCoordinator = null
            card.dragGhost.sourceItem = null
            card.dragGhost.parkedAtSource = false
            card.dragGhost.animateToSource = false
            card.dragEnded()
        }
    }

    Column {
        id: contentColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        // Wide enough to clear the volume corner button (32px + 8px
        // margin), since content can run the full height below it.
        anchors.rightMargin: 48
        spacing: 12

        Item {
            id: statusRow
            readonly property bool isPlaying: card.coordinator && card.coordinator.playStateText === "Playing"
            readonly property string stateText: card.coordinator ? card.coordinator.playStateText : ""
            readonly property var track: card.coordinator ? card.coordinator.currentTrack : null
            readonly property string titleArtistText: {
                if (!track || !track.title)
                    return ""
                return track.artist ? (track.title + " • " + track.artist) : track.title
            }
            readonly property string artistTitleText: {
                if (!track || !track.title)
                    return ""
                return track.artist ? (track.artist + " • " + track.title) : track.title
            }
            readonly property bool canCycle: isPlaying && titleArtistText.length > 0
            readonly property int textLeft: playStateIcon.width + 8
            readonly property int textWidth: Math.max(0, width - textLeft)
            readonly property int longPauseMs: 5000
            readonly property int briefPauseMs: 1500
            readonly property real pixelsPerSecond: 42
            property int statusPhase: 0
            property int pendingStatusPhase: 0
            property bool animating: false
            property bool fadingToState: false
            property string currentStatusText: stateText
            property string nextStatusText: ""

            width: parent.width
            height: 20

            onStateTextChanged: resetStatusCycle()
            onTitleArtistTextChanged: resetStatusCycle()
            onArtistTitleTextChanged: resetStatusCycle()
            onCanCycleChanged: resetStatusCycle()

            function resetStatusCycle() {
                transitionTimer.stop()
                statusSlideAnimation.stop()
                statusFadeAnimation.stop()
                animating = false
                fadingToState = false
                statusPhase = 0
                pendingStatusPhase = 0
                currentStatusText = stateText
                nextStatusText = ""
                currentStatusTextItem.x = 0
                currentStatusTextItem.opacity = 0.7
                nextStatusTextItem.x = statusTextViewport.width
                nextStatusTextItem.opacity = 0.7
                transitionTimer.interval = longPauseMs
                if (canCycle)
                    transitionTimer.restart()
            }

            function textForPhase(phase) {
                if (phase === 1)
                    return titleArtistText
                if (phase === 2)
                    return artistTitleText
                return stateText
            }

            function startStatusTransition() {
                if (!canCycle || animating)
                    return
                pendingStatusPhase = statusPhase === 0 ? 1 : (statusPhase === 1 ? 2 : 0)
                animating = true
                fadingToState = statusPhase === 0 || pendingStatusPhase === 0
                nextStatusText = textForPhase(pendingStatusPhase)
                if (fadingToState) {
                    currentStatusTextItem.opacity = 0.7
                    nextStatusTextItem.x = 0
                    nextStatusTextItem.opacity = 0
                    statusFadeAnimation.start()
                } else {
                    nextStatusTextItem.x = statusTextViewport.width
                    nextStatusTextItem.opacity = 0.7
                    statusSlideAnimation.start()
                }
            }

            Timer {
                id: transitionTimer
                interval: 5000
                repeat: false
                onTriggered: statusRow.startStatusTransition()
            }

            Item {
                id: playStateIcon
                width: 18
                height: 16
                x: 0
                y: currentStatusTextItem.y + currentStatusTextItem.baselineOffset - height

                EqualizerIcon {
                    anchors.centerIn: parent
                    visible: statusRow.isPlaying
                    running: visible
                    barColor: card.hasAccent ? card.contrastColor : "#4CAF50"
                }

                Rectangle {
                    width: 10
                    height: 10
                    radius: 2
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    visible: !statusRow.isPlaying
                    color: card.hasAccent ? card.contrastColor : "#616161"
                }
            }

            Item {
                id: statusTextViewport
                x: statusRow.textLeft
                width: statusRow.textWidth
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                clip: true

                Text {
                    id: currentStatusTextItem
                    width: statusTextViewport.width
                    anchors.verticalCenter: parent.verticalCenter
                    text: statusRow.currentStatusText
                    font.pixelSize: 12
                    color: card.contrastColor
                    opacity: 0.7
                    elide: Text.ElideRight
                }

                Text {
                    id: nextStatusTextItem
                    x: statusTextViewport.width
                    width: statusTextViewport.width
                    anchors.verticalCenter: parent.verticalCenter
                    text: statusRow.nextStatusText
                    font.pixelSize: currentStatusTextItem.font.pixelSize
                    color: currentStatusTextItem.color
                    opacity: 0.7
                    elide: Text.ElideRight
                }
            }

            ParallelAnimation {
                id: statusSlideAnimation

                NumberAnimation {
                    target: currentStatusTextItem
                    property: "x"
                    to: -statusTextViewport.width
                    duration: Math.max(2400,
                                       Math.round(statusTextViewport.width / statusRow.pixelsPerSecond * 1000))
                    easing.type: Easing.InOutSine
                }

                NumberAnimation {
                    target: nextStatusTextItem
                    property: "x"
                    to: 0
                    duration: Math.max(2400,
                                       Math.round(statusTextViewport.width / statusRow.pixelsPerSecond * 1000))
                    easing.type: Easing.InOutSine
                }

                onFinished: statusRow.finishStatusTransition()
            }

            ParallelAnimation {
                id: statusFadeAnimation

                NumberAnimation {
                    target: currentStatusTextItem
                    property: "opacity"
                    to: 0
                    duration: 450
                    easing.type: Easing.OutQuad
                }

                NumberAnimation {
                    target: nextStatusTextItem
                    property: "opacity"
                    to: 0.7
                    duration: 700
                    easing.type: Easing.InOutQuad
                }

                onFinished: statusRow.finishStatusTransition()
            }

            function finishStatusTransition() {
                statusPhase = pendingStatusPhase
                currentStatusText = nextStatusText
                nextStatusText = ""
                currentStatusTextItem.x = 0
                currentStatusTextItem.opacity = 0.7
                nextStatusTextItem.x = statusTextViewport.width
                nextStatusTextItem.opacity = 0.7
                animating = false
                transitionTimer.interval = statusPhase === 1 ? briefPauseMs : longPauseMs
                if (canCycle)
                    transitionTimer.restart()
            }
        }

        Repeater {
            model: card.members

            delegate: Column {
                id: memberBlock
                width: contentColumn.width
                spacing: 6

                required property var modelData
                required property int index
                readonly property var zone: modelData

                Rectangle {
                    width: parent.width
                    height: 1
                    color: Qt.rgba(card.contrastColor.r, card.contrastColor.g, card.contrastColor.b, 0.12)
                    visible: memberBlock.index > 0
                }

                Row {
                    width: parent.width
                    spacing: 10

                    // Per-card settings/cog affordance, placed before each
                    // zone title so grouped zones get the same affordance as
                    // single-zone cards.
                    Item {
                        id: cardSettingsButton
                        width: 16
                        height: 24
                        anchors.verticalCenter: parent.verticalCenter
                        opacity: cardSettingsMouseArea.containsMouse ? 1.0 : 0.3

                        Behavior on opacity {
                            NumberAnimation { duration: 150 }
                        }

                        Rectangle {
                            id: cardSettingsHoverCircle
                            width: 24
                            height: 24
                            radius: 12
                            x: (cardSettingsButton.width - width) / 2
                            anchors.verticalCenter: parent.verticalCenter
                            color: cardSettingsMouseArea.pressed
                                   ? Qt.rgba(card.contrastColor.r, card.contrastColor.g, card.contrastColor.b, 0.18)
                                   : (cardSettingsMouseArea.containsMouse
                                      ? Qt.rgba(card.contrastColor.r, card.contrastColor.g, card.contrastColor.b, 0.10)
                                      : "transparent")
                        }

                        Image {
                            anchors.centerIn: parent
                            source: card.backgroundIsLight ? "../resources/icons/settings.svg" : "../resources/icons/settings_light.svg"
                            sourceSize.width: 16
                            sourceSize.height: 16
                        }

                        MouseArea {
                            id: cardSettingsMouseArea
                            anchors.fill: cardSettingsHoverCircle
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: zoneSettingsMenu.open()
                        }

                        ActionMenu {
                            id: zoneSettingsMenu
                            parent: cardSettingsButton
                            x: 0
                            y: cardSettingsButton.height + 4
                            items: [qsTr("Leave zone group")]

                            onItemClicked: card.unlinkRequested(memberBlock.zone)
                        }
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: memberBlock.zone ? memberBlock.zone.roomName : ""
                        font.pixelSize: Math.round(16 * UiScale.factor)
                        font.weight: Typography.emphasisWeight
                        color: card.contrastColor
                    }
                }
            }
        }

    }


}
