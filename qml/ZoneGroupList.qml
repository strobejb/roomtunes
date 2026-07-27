import QtQuick
import QtQuick.Controls

// The Zones panel's scrollable card list -- owns the ListView itself,
// the shared drag ghost's positioning/hit-testing logic (autoscroll near
// the list's edges, figuring out which card if any is currently a valid
// drop target), and the busy-spinner overlay shown while a group/unlink
// request is in flight. Extracted out of Main.qml, which was
// accumulating a lot of drag-and-drop plumbing that's specific to this
// one list and nothing else in the window.
Item {
    id: root

    // Shared across every ZoneGroupCard delegate -- see
    // ZoneDragGhost.qml's own comment for why this is passed in from
    // Main.qml rather than owned here.
    property Item dragGhost: null
    property var selectedZone: null
    signal zoneSelected(var zone)

    function normalizeSelection() {
        const coordinator = groupsModel.canonicalCoordinator(root.selectedZone)
        if (coordinator !== root.selectedZone)
            root.zoneSelected(coordinator)
    }

    ListView {
        id: zoneListView
        anchors.fill: parent
        clip: true
        spacing: 10
        model: groupsModel
        // Faded and fully inert while processing -- enabled: false (not
        // just the opacity) is what actually matters here: it's what
        // stops hover states and clicks from reaching the cards
        // underneath, since opacity alone is purely visual and wouldn't
        // block input at all.
        opacity: processing ? 0.5 : 1.0
        enabled: !processing

        Behavior on opacity {
            NumberAnimation { duration: 150 }
        }

        delegate: ZoneGroupCard {
            id: zoneCard
            width: zoneListView.width
            dragGhost: root.dragGhost
            coordinator: model.coordinator
            members: model.members
            selected: root.selectedZone === model.coordinator

            onClicked: root.zoneSelected(model.coordinator)
            // Scrolls the list to reveal an off-screen drop target, and
            // re-evaluates which (if any) card the mouse is currently
            // over -- see ZoneGroupCard.qml's own dragPositionChanged
            // comment for why this lives here rather than inside the
            // card itself (it owns zoneListView, the card doesn't).
            onDragPositionChanged: globalPos => {
                zoneListView.autoScrollForDrag(globalPos)
                zoneListView.updateDropTarget(globalPos)
            }
            onDragEnded: {
                zoneListView.stopAutoScroll()
                zoneListView.clearAllHighlights()
            }
            // Joins source's zone into target's group -- see
            // ZonePlayer::joinGroup. Marks the list as processing until
            // the resulting topology change actually lands (see
            // zoneListView.processing's own comment).
            onDropRequested: (source, target) => {
                if (source && target) {
                    zoneListView.processing = true
                    source.joinGroup(target)
                }
            }
            // Removes this one zone from whatever group it's in -- see
            // ZonePlayer::leaveGroup.
            onUnlinkRequested: (zone) => {
                if (zone) {
                    zoneListView.processing = true
                    zone.leaveGroup()
                }
            }
            Component.onCompleted: root.normalizeSelection()
        }

        Label {
            anchors.centerIn: parent
            visible: zoneListView.count === 0
            text: qsTr("No zones found yet…")
            color: "#9E9E9E"
            font.pixelSize: 13
        }

        // Speed ramps up the closer the drag gets to the edge, so a drag
        // that's barely over the threshold scrolls gently while one
        // right at the edge scrolls quickly.
        readonly property int autoScrollEdgeZone: 40
        readonly property real autoScrollMaxSpeed: 10

        Timer {
            id: autoScrollTimer
            interval: 16
            repeat: true
            property real speed: 0
            onTriggered: {
                zoneListView.contentY = Math.max(0,
                    Math.min(Math.max(0, zoneListView.contentHeight - zoneListView.height),
                              zoneListView.contentY + speed))
            }
        }

        function autoScrollForDrag(globalPos) {
            // dragGhost.parent, not null/window space -- matches
            // ZoneGroupCard.qml's own dragPositionChanged, which reports
            // positions in that same space (see its own comment for
            // why).
            var localPos = zoneListView.mapFromItem(root.dragGhost.parent, globalPos.x, globalPos.y)
            var edge = autoScrollEdgeZone
            if (localPos.y < edge) {
                autoScrollTimer.speed = -autoScrollMaxSpeed * (1 - localPos.y / edge)
                autoScrollTimer.start()
            } else if (localPos.y > zoneListView.height - edge) {
                autoScrollTimer.speed = autoScrollMaxSpeed * (1 - (zoneListView.height - localPos.y) / edge)
                autoScrollTimer.start()
            } else {
                autoScrollTimer.stop()
            }
        }

        function stopAutoScroll() {
            autoScrollTimer.stop()
        }

        // How far in from each card's top/bottom edges the mouse has to
        // be for that card to count as a drop target -- keeps the gap
        // between cards from being read as "over" either one, while the
        // full card width remains targetable.
        readonly property int dropTargetMargin: 18

        // Drop-target eligibility is based on where the *mouse* is, not
        // on where this (possibly hidden, see dragGhost's own comment)
        // dragged ghost rectangle's bounds happen to overlap -- so this
        // hit-tests every card directly against globalPos instead of
        // leaning on DropArea/Drag overlap. globalPos is in
        // dragGhost.parent's coordinate space, same as
        // autoScrollForDrag above.
        function updateDropTarget(globalPos) {
            var localPos = zoneListView.mapFromItem(root.dragGhost.parent, globalPos.x, globalPos.y)
            var insideList = localPos.x >= 0 && localPos.x <= zoneListView.width
                              && localPos.y >= 0 && localPos.y <= zoneListView.height
            var wasInsideList = root.dragGhost.insideList
            root.dragGhost.insideList = insideList
            root.dragGhost.parkedAtSource = !insideList
            if (!insideList) {
                if (root.dragGhost.sourceItem) {
                    var sourcePos = root.dragGhost.sourceItem.mapToItem(root.dragGhost.parent, 0, 0)
                    root.dragGhost.sourceX = sourcePos.x
                    root.dragGhost.sourceY = sourcePos.y
                }
                root.dragGhost.animateToSource = wasInsideList
                root.dragGhost.x = root.dragGhost.sourceX
                root.dragGhost.y = root.dragGhost.sourceY
            } else {
                root.dragGhost.animateToSource = false
            }

            var hitCoordinator = null
            if (insideList) {
                var margin = zoneListView.dropTargetMargin
                for (var i = 0; i < zoneListView.count; i++) {
                    var item = zoneListView.itemAtIndex(i)
                    if (!item)
                        continue
                    var topLeft = item.mapToItem(root.dragGhost.parent, 0, 0)
                    if (globalPos.x >= topLeft.x
                            && globalPos.x <= topLeft.x + item.width
                            && globalPos.y >= topLeft.y + margin
                            && globalPos.y <= topLeft.y + item.height - margin) {
                        hitCoordinator = item.coordinator
                        break
                    }
                }
            }

            // A card can't be a drop target for its own drag.
            if (hitCoordinator === root.dragGhost.sourceCoordinator)
                hitCoordinator = null

            root.dragGhost.targetCoordinator = hitCoordinator
            zoneListView.applyHighlight(hitCoordinator)
        }

        function applyHighlight(targetCoordinator) {
            for (var i = 0; i < zoneListView.count; i++) {
                var item = zoneListView.itemAtIndex(i)
                if (item)
                    item.dropHighlighted = (targetCoordinator !== null && item.coordinator === targetCoordinator)
            }
        }

        function clearAllHighlights() {
            zoneListView.applyHighlight(null)
        }

        // True from the moment a group/unlink action is requested until
        // the *next* topology rebuild lands (see the Connections below)
        // -- covers the real gap between "we asked Sonos to regroup" and
        // "the UI actually reflects it", since
        // SetAVTransportURI/BecomeCoordinatorOfStandaloneGroup completing
        // just means the command was accepted, not that the new
        // grouping has shown up yet.
        property bool processing: false

        // groupsModel resets (beginResetModel/endResetModel) every time
        // it rebuilds from a real topology change -- see
        // GroupedZoneModel::rebuild(). That's also triggered by
        // unrelated per-zone signals (roomNameChanged/readyChanged/...),
        // not only grouping, so this clears processing on the *first*
        // rebuild after an action was requested rather than confirming
        // it's specifically the expected one -- simple, and correct in
        // practice since a stray unrelated rebuild landing in the same
        // few-second window is rare.
        Connections {
            target: groupsModel
            function onModelReset() {
                zoneListView.processing = false
                root.normalizeSelection()
            }
        }
    }

    // Busy indicator shown while processing -- zoneListView's own
    // opacity/enabled bindings above already handle fading and fully
    // disabling the list (including blocking hover, not just clicks), so
    // this is purely the visual "something's happening" cue on top of
    // it, not a second input blocker.
    BusySpinner {
        anchors.centerIn: parent
        running: zoneListView.processing
    }
}
