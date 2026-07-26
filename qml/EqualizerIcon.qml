import QtQuick

Item {
    id: root

    property color barColor: "#4CAF50"
    property bool running: true

    width: 18
    height: 16

    Row {
        anchors.fill: parent
        spacing: 2

        Repeater {
            model: [
                { low: 0.25, high: 0.85, up: 240, down: 460 },
                { low: 0.45, high: 1.00, up: 310, down: 390 },
                { low: 0.20, high: 0.70, up: 220, down: 430 },
                { low: 0.35, high: 0.95, up: 290, down: 360 },
                { low: 0.30, high: 0.80, up: 250, down: 440 }
            ]

            Rectangle {
                required property var modelData

                width: 2
                height: root.height * modelData.low
                radius: 1
                anchors.bottom: parent.bottom
                color: root.barColor

                SequentialAnimation on height {
                    running: root.running
                    loops: Animation.Infinite

                    NumberAnimation {
                        to: root.height * modelData.high
                        duration: modelData.up
                        easing.type: Easing.InOutQuad
                    }

                    NumberAnimation {
                        to: root.height * modelData.low
                        duration: modelData.down
                        easing.type: Easing.InOutQuad
                    }
                }
            }
        }
    }
}
