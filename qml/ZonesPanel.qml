import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The "Zones" column's full content -- header (title + settings menu)
// and the card list below it. Pulled out of Main.qml, which had
// accumulated a lot of drag-and-drop- and topology-specific plumbing
// that's local to this one column and nothing else in the window.
Item {
    id: root

    property Item dragGhost: null
    property var selectedZone: null
    signal zoneSelected(var zone)

    // Exposed so Main.qml can size the Browse column's own bottom
    // settings-icon row to match this header row's height (see its own
    // comment there), and fold this panel's own settings-menu visibility
    // into the window-wide dim behind an open ActionMenu.
    readonly property alias headerHeight: zonesHeaderRow.height
    readonly property alias settingsMenuOpen: zonesSettingsMenu.visible

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        RowLayout {
            id: zonesHeaderRow
            Layout.fillWidth: true
            spacing: 4

            Label {
                Layout.fillWidth: true
                text: qsTr("Zones")
                font.pixelSize: Math.round(18 * UiScale.factor)
                font.weight: Typography.emphasisWeight
                color: "#212121"
            }

            IconButton {
                id: zonesSettingsButton
                iconSource: "../resources/icons/settings.svg"
                onPressed: zonesSettingsMenu.open()
            }

            // Right-aligned to the button (opens leftward) since the
            // button sits at the Zones column's own right edge, which
            // clips -- opening left-aligned would run the menu straight
            // off the panel.
            ActionMenu {
                id: zonesSettingsMenu
                parent: zonesSettingsButton
                x: zonesSettingsButton.width - width
                y: zonesSettingsButton.height + 6
                items: [qsTr("Pause All"), "-", qsTr("Add new zone")]
            }
        }

        ZoneGroupList {
            Layout.fillWidth: true
            Layout.fillHeight: true
            dragGhost: root.dragGhost
            selectedZone: root.selectedZone
            onZoneSelected: (zone) => root.zoneSelected(zone)
        }
    }
}
