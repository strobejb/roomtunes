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

    // Emitted by the corner settings/cog icon. No per-card settings menu
    // exists yet, so this just surfaces the intent, same as
    // unlinkRequested/dropRequested above.
    signal settingsRequested()

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

    // Click-to-select AND press-and-drag-to-group share this one
    // MouseArea, covering the whole card -- the icon buttons layered on
    // top of it (volume/settings/unlink below) get first refusal on any
    // press within their own smaller bounds, so "anywhere except the
    // buttons" falls out of ordinary Qt Quick hit-testing rather than
    // needing an explicit exclusion. clicked() and drag.target are safe
    // to combine on one MouseArea: Qt Quick only starts a drag once the
    // press has moved past its own drag threshold, and suppresses
    // clicked() for that press once it does -- so a plain tap still
    // selects the zone, and only a genuine drag ever reveals the ghost
    // (see drag.onActiveChanged below).
    MouseArea {
        id: cardMouseArea
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        drag.target: card.dragGhost
        drag.axis: Drag.YAxis

        onClicked: card.clicked()

        // Position/size the (still invisible) ghost on every press, not
        // just real drags -- drag.target reads the target's *current*
        // position as its own baseline the moment the press starts, well
        // before drag.active turns true. Deferring this to
        // drag.onActiveChanged (as an earlier version did) left the ghost
        // wherever the *previous* drag had ended -- often nowhere near
        // this card -- since drag.target had already latched onto that
        // stale position as its baseline by the time this ran. Cheap and
        // harmless to do unconditionally: a plain click never reveals the
        // ghost (see below), so this never has a visible effect on its own.
        onPressed: card.prepareDrag()

        // Only a real drag (past Qt's own threshold) reveals the ghost --
        // a plain click's press/release happens too close together for
        // startDrag()'s dashed/dimmed state to ever actually render, but a
        // slow deliberate click-and-hold-without-moving would otherwise
        // flash it for no reason.
        drag.onActiveChanged: {
            if (cardMouseArea.drag.active)
                card.revealDrag()
            else
                card.endDrag()
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
    // is worth seeing at a glance rather than only on hover. Icon glyph
    // mirrors NowPlayingVolumeControl.qml's own volume/mute threshold
    // logic, but as a plain click-to-mute button rather than that
    // component's hover-expanding slider: this card is far too short to
    // fit that control's fixed 154px-tall footprint without it
    // overlapping neighboring cards.
    Rectangle {
        id: volumeButton
        width: 32
        height: 32
        radius: 16
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 8
        color: volumeMouseArea.pressed
               ? Qt.rgba(card.contrastColor.r, card.contrastColor.g, card.contrastColor.b, 0.18)
               : (volumeMouseArea.containsMouse
                  ? Qt.rgba(card.contrastColor.r, card.contrastColor.g, card.contrastColor.b, 0.10)
                  : "transparent")

        readonly property string volumeIconName: {
            if (!card.coordinator)
                return "volume_1"
            if (card.coordinator.muted)
                return "volume_x"
            if (card.coordinator.volume < 5)
                return "volume_0"
            if (card.coordinator.volume < 20)
                return "volume_1"
            return "volume_2"
        }

        Image {
            anchors.centerIn: parent
            source: card.backgroundIsLight
                    ? "../resources/icons/" + volumeButton.volumeIconName + ".svg"
                    : "../resources/icons/" + volumeButton.volumeIconName + "_light.svg"
            sourceSize.width: 18
            sourceSize.height: 18
        }

        MouseArea {
            id: volumeMouseArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: if (card.coordinator) card.coordinator.setMuted(!card.coordinator.muted)
        }
    }

    // Settings/cog icon, bottom-right corner -- only shown at all while
    // hovering the card (unlike the always-visible volume icon), and
    // even then sits at half strength until the pointer is over the
    // icon itself, which brings it to full strength. No per-card
    // settings menu exists yet, so this just surfaces the intent via
    // settingsRequested() for now.
    Rectangle {
        id: cardSettingsButton
        width: 32
        height: 32
        radius: 16
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 8
        visible: card.cardHovered
        opacity: cardSettingsMouseArea.containsMouse ? 1.0 : 0.5
        color: cardSettingsMouseArea.pressed
               ? Qt.rgba(card.contrastColor.r, card.contrastColor.g, card.contrastColor.b, 0.18)
               : (cardSettingsMouseArea.containsMouse
                  ? Qt.rgba(card.contrastColor.r, card.contrastColor.g, card.contrastColor.b, 0.10)
                  : "transparent")

        Behavior on opacity {
            NumberAnimation { duration: 150 }
        }

        Image {
            anchors.centerIn: parent
            source: card.backgroundIsLight ? "../resources/icons/settings.svg" : "../resources/icons/settings_light.svg"
            sourceSize.width: 18
            sourceSize.height: 18
        }

        MouseArea {
            id: cardSettingsMouseArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: card.settingsRequested()
        }
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
        card.dragGhost.targetCoordinator = null
        card.dragGhost.insideList = true
        var startPos = card.mapToItem(card.dragGhost.parent, 0, 0)
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
            card.dragEnded()
        }
    }

    Column {
        id: contentColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        // Wide enough to clear the volume/settings corner buttons
        // (32px + 8px margin each) at both the top and bottom of the
        // card, since content can run the full height between them.
        anchors.rightMargin: 48
        spacing: 8

        Row {
            width: parent.width
            spacing: 8

            Rectangle {
                width: 10
                height: 10
                radius: 5
                anchors.verticalCenter: parent.verticalCenter
                // Only overridden to plain contrastColor while showing an
                // accent-colored card -- otherwise this keeps its normal
                // ready/not-ready meaning.
                color: card.hasAccent
                       ? card.contrastColor
                       : (card.coordinator && card.coordinator.ready ? "#4CAF50" : "#BDBDBD")
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: card.coordinator ? (card.coordinator.playStateText + " · Vol " + card.coordinator.volume) : ""
                font.pixelSize: 12
                color: card.contrastColor
                opacity: 0.7
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
                    spacing: 6

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: memberBlock.zone ? memberBlock.zone.roomName : ""
                        font.pixelSize: Math.round(16 * UiScale.factor)
                        font.weight: Typography.emphasisWeight
                        color: card.contrastColor
                    }

                    // Removes just this one zone from the group -- only
                    // meaningful (and only shown) when there's more than
                    // one zone to unlink from, and even then only while
                    // hovering the card.
                    Rectangle {
                        id: unlinkButton
                        width: 24
                        height: 24
                        radius: 12
                        anchors.verticalCenter: parent.verticalCenter
                        visible: card.cardHovered && card.members.length > 1
                        color: unlinkMouseArea.pressed
                               ? Qt.rgba(card.contrastColor.r, card.contrastColor.g, card.contrastColor.b, 0.18)
                               : (unlinkMouseArea.containsMouse
                                  ? Qt.rgba(card.contrastColor.r, card.contrastColor.g, card.contrastColor.b, 0.10)
                                  : "transparent")

                        Image {
                            anchors.centerIn: parent
                            source: card.backgroundIsLight ? "../resources/icons/unlink.svg" : "../resources/icons/unlink_light.svg"
                            sourceSize.width: 15
                            sourceSize.height: 15
                        }

                        MouseArea {
                            id: unlinkMouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: card.unlinkRequested(memberBlock.zone)
                        }
                    }
                }
            }
        }

    }


}
