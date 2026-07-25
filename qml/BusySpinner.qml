import QtQuick
import QtQuick.Effects
import QtQuick.Shapes

// A generic "something's happening, please wait" busy indicator -- a
// white rounded chip (matching ActionMenu.qml's own popup styling, so it
// reads clearly regardless of whatever's behind it) with a rotating arc
// inside. Not zone-specific despite ZoneGroupList.qml being its only
// user today.
Item {
    id: root

    property bool running: false

    width: 56
    height: 56
    visible: running

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: "white"

        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: "#40000000"
            shadowBlur: 0.6
        }
    }

    Item {
        anchors.centerIn: parent
        width: 28
        height: 28

        RotationAnimation on rotation {
            running: root.running
            from: 0
            to: 360
            duration: 900
            loops: Animation.Infinite
        }

        Shape {
            anchors.fill: parent
            antialiasing: true

            ShapePath {
                strokeColor: "#424242"
                strokeWidth: 4
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap

                PathAngleArc {
                    centerX: 14
                    centerY: 14
                    radiusX: 12
                    radiusY: 12
                    startAngle: 0
                    sweepAngle: 270
                }
            }
        }
    }
}
