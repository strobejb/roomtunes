import QtQuick
import QtQuick.Effects

Item {
    id: root

    property string text: ""
    property color color: "#212121"
    property real textOpacity: 1.0
    property int pixelSize: 14
    property int weight: Font.Normal
    property int horizontalAlignment: Text.AlignLeft
    property int pauseMs: 5000
    property int minimumScrollMs: 2400
    property real pixelsPerSecond: 42
    property int repeatGap: 36
    property int fadeWidth: 32

    readonly property bool shouldScroll:
        width > 0 && text.length > 0 && elidedText.truncated
    readonly property real scrollDistance: marqueeTextA.implicitWidth + repeatGap

    implicitHeight: elidedText.implicitHeight
    clip: true

    onTextChanged: resetScroll()
    onWidthChanged: resetScroll()
    onShouldScrollChanged: resetScroll()

    function resetScroll() {
        scrollTimer.stop()
        scrollAnimation.stop()
        marqueeStrip.x = 0
        if (shouldScroll)
            scrollTimer.restart()
    }

    Text {
        id: elidedText
        width: root.width
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        font.pixelSize: root.pixelSize
        font.weight: root.weight
        color: root.color
        opacity: root.textOpacity
        elide: Text.ElideRight
        horizontalAlignment: root.horizontalAlignment
        visible: !scrollAnimation.running && !root.shouldScroll
    }

    Item {
        id: fadedTextSource
        anchors.fill: parent
        clip: true
        layer.enabled: true
        visible: false

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            font.pixelSize: root.pixelSize
            font.weight: root.weight
            color: root.color
            opacity: root.textOpacity
            elide: Text.ElideNone
        }
    }

    Item {
        id: fadeMask
        anchors.fill: parent
        layer.enabled: true
        visible: false

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: Math.max(0, parent.width - root.fadeWidth)
            color: "black"
        }

        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: Math.min(parent.width, root.fadeWidth)
            gradient: Gradient {
                orientation: Gradient.Horizontal

                GradientStop {
                    position: 0
                    color: "black"
                }

                GradientStop {
                    position: 1
                    color: "transparent"
                }
            }
        }
    }

    MultiEffect {
        anchors.fill: parent
        visible: !scrollAnimation.running && root.shouldScroll
        source: fadedTextSource
        maskEnabled: true
        maskSource: fadeMask
    }

    Item {
        id: marqueeStrip
        anchors.verticalCenter: parent.verticalCenter
        width: marqueeTextA.implicitWidth * 2 + root.repeatGap
        height: marqueeTextA.implicitHeight
        visible: scrollAnimation.running

        Text {
            id: marqueeTextA
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            font.pixelSize: root.pixelSize
            font.weight: root.weight
            color: root.color
            opacity: root.textOpacity
            elide: Text.ElideNone
        }

        Text {
            anchors.left: marqueeTextA.right
            anchors.leftMargin: root.repeatGap
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            font.pixelSize: root.pixelSize
            font.weight: root.weight
            color: root.color
            opacity: root.textOpacity
            elide: Text.ElideNone
        }
    }

    Timer {
        id: scrollTimer
        interval: root.pauseMs
        repeat: false
        onTriggered: {
            if (root.shouldScroll)
                scrollAnimation.start()
        }
    }

    SequentialAnimation {
        id: scrollAnimation

        ScriptAction {
            script: marqueeStrip.x = 0
        }

        NumberAnimation {
            target: marqueeStrip
            property: "x"
            to: -root.scrollDistance
            duration: Math.max(root.minimumScrollMs,
                               Math.round(root.scrollDistance / root.pixelsPerSecond * 1000))
            easing.type: Easing.InOutSine
        }

        onFinished: root.resetScroll()
    }

    Component.onCompleted: resetScroll()
}
