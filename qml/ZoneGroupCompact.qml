import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

// Narrow-column zones surface. Collapsed, it is a single selected-zone bar;
// expanded, it becomes the normal zone-card list without the standalone
// "Zones" header used by the full left column.
Item {
    id: root

    property Item dragGhost: null
    property var selectedZone: null
    property bool expanded: false
    signal zoneSelected(var zone)
    signal expandRequested()

    implicitHeight: expanded ? 240 : 44
    clip: true

    readonly property bool hasAccent: !!root.selectedZone && root.selectedZone.accentColor.a > 0
    readonly property color panelBackground: root.expanded || !hasAccent
        ? "#F5F5F5"
        : root.selectedZone.accentColor
    readonly property bool backgroundIsLight: luminance(panelBackground) > 0.55
    readonly property color contrastColor: backgroundIsLight ? "#212121" : "white"
    readonly property alias settingsMenuOpen: zonesSettingsMenu.visible

    function luminance(c) {
        return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b
    }

    function blendToward(base, target, amount) {
        return Qt.rgba(
            base.r + (target.r - base.r) * amount,
            base.g + (target.g - base.g) * amount,
            base.b + (target.b - base.b) * amount,
            1.0)
    }

    readonly property color hoverColor: backgroundIsLight
        ? blendToward(panelBackground, Qt.rgba(0, 0, 0, 1), 0.08)
        : blendToward(panelBackground, Qt.rgba(1, 1, 1, 1), 0.12)

    Rectangle {
        id: panel
        anchors.fill: parent
        radius: root.expanded ? 12 : height / 2
        color: headerMouseArea.containsMouse && !root.expanded ? root.hoverColor : root.panelBackground

        Behavior on color {
            ColorAnimation { duration: 400 }
        }

        Behavior on radius {
            NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
        }

        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: "#22000000"
            shadowBlur: 0.5
            shadowVerticalOffset: 2
        }
    }

    MouseArea {
        id: headerMouseArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 44
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.expandRequested()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.expanded ? 10 : 0
        spacing: root.expanded ? 10 : 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            Layout.leftMargin: root.expanded ? 4 : 14
            Layout.rightMargin: root.expanded ? 0 : 6
            spacing: 8

            Rectangle {
                width: 8
                height: 8
                radius: 4
                color: root.hasAccent && !root.expanded
                       ? root.contrastColor
                       : (root.selectedZone && root.selectedZone.ready ? "#4CAF50" : "#BDBDBD")
            }

            Label {
                Layout.fillWidth: true
                text: root.selectedZone ? root.selectedZone.roomName : qsTr("No zone selected")
                font.pixelSize: 14
                font.weight: Typography.emphasisWeight
                color: root.contrastColor
                elide: Text.ElideRight
            }

            Image {
                visible: !root.expanded
                source: root.backgroundIsLight
                        ? "../resources/icons/triangle_down.svg"
                        : "../resources/icons/triangle_down_light.svg"
                sourceSize.width: 10
                sourceSize.height: 10
            }

            IconButton {
                id: zonesSettingsButton
                Layout.alignment: Qt.AlignVCenter
                iconSource: root.backgroundIsLight
                            ? "../resources/icons/settings.svg"
                            : "../resources/icons/settings_light.svg"
                idleColor: "transparent"
                hoverColor: root.backgroundIsLight ? "#E0E0E0" : "#FFFFFF33"
                pressedColor: root.backgroundIsLight ? "#D0D0D0" : "#FFFFFF55"
                onPressed: zonesSettingsMenu.open()
            }
        }

        ZoneGroupList {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.expanded
            dragGhost: root.dragGhost
            selectedZone: root.selectedZone
            onZoneSelected: (zone) => root.zoneSelected(zone)
        }
    }

    ActionMenu {
        id: zonesSettingsMenu
        parent: zonesSettingsButton
        x: zonesSettingsButton.width - width
        y: zonesSettingsButton.height + 6
        items: [qsTr("Mute All")]
    }
}
