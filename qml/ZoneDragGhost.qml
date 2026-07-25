import QtQuick
import QtQuick.Shapes

// The shared drag-and-drop ghost for the Zones list -- one instance, kept
// alive statically by Main.qml (inside its own dragOverlayLayer, above
// everything else so the ghost isn't clipped by the zone list the moment
// it moves outside whichever card started the drag) and handed down to
// every ZoneGroupCard via their own dragGhost property. One
// statically-placed shared item rather than each card owning (and
// dynamically reparenting into that layer) its own -- a dynamically
// reparented-in item never actually rendered in this app's own testing
// despite every property (visible, parent, geometry, opacity, z) being
// provably correct, while a statically-placed item, merely driven by
// properties, reliably did.
Item {
    id: root

    // dragging: true for the whole press-to-release gesture.
    // insideList: whether the mouse's last known position was within
    // ZoneGroupList.qml's own viewport (see its updateDropTarget()) --
    // moving the mouse out of the list hides the ghost entirely rather
    // than leaving it dangling over unrelated UI, since there's nothing
    // to drop onto out there.
    property bool dragging: false
    property bool insideList: true
    visible: dragging && insideList
    opacity: 0.9
    z: 1000

    property var sourceCoordinator: null
    // Set by ZoneGroupList.qml's updateDropTarget() from the actual
    // mouse position (see its own comment for why -- NOT from where this
    // ghost's own bounds happen to overlap a card).
    property var targetCoordinator: null
    readonly property bool solid: targetCoordinator !== null

    readonly property color strokeColor: "#424242"
    readonly property int strokeWidth: 2
    // Matches ZoneGroupCard.qml's own radius: 12 -- can't reference
    // card.radius directly since this item isn't inside any particular
    // card's own document scope.
    readonly property int cornerRadius: 12

    // Dashed rounded-rect outline -- only ever the ghost's own indicator,
    // shown while not over a valid drop target. Once solid becomes true,
    // this hides entirely rather than switching to a solid outline of
    // its own: the target card draws its own solid highlight (see
    // ZoneGroupCard.qml's dropHighlighted Rectangle) so only one
    // rectangle is ever on screen at a time.
    Shape {
        anchors.fill: parent
        visible: !root.solid
        antialiasing: true

        ShapePath {
            strokeColor: root.strokeColor
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            strokeStyle: ShapePath.DashLine
            dashPattern: [3, 2]
            capStyle: ShapePath.FlatCap
            joinStyle: ShapePath.RoundJoin

            // Rounded-rect path built by hand (QtQuick.Shapes has no
            // PathRoundedRect element) -- four straight edges joined by
            // quarter-circle arcs sized to cornerRadius, traced
            // clockwise from the top edge.
            PathSvg {
                path: {
                    var w = root.width
                    var h = root.height
                    var r = root.cornerRadius
                    return "M " + r + ",0" +
                           " L " + (w - r) + ",0" +
                           " A " + r + "," + r + " 0 0 1 " + w + "," + r +
                           " L " + w + "," + (h - r) +
                           " A " + r + "," + r + " 0 0 1 " + (w - r) + "," + h +
                           " L " + r + "," + h +
                           " A " + r + "," + r + " 0 0 1 0," + (h - r) +
                           " L 0," + r +
                           " A " + r + "," + r + " 0 0 1 " + r + ",0" +
                           " Z"
                }
            }
        }
    }
}
