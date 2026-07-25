import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects

// A slim "which zone is this controlling" bar -- shown in place of the
// full Zones list (see Main.qml's zonesColumn) once that's hidden for
// being too narrow. Sits directly under Now Playing/Queue, which are
// already scoped to whichever zone is selected, so "here's which zone
// this is" reads naturally right there instead of resurfacing the full
// room list somewhere unrelated. Tapping it opens a compact picker
// rather than reusing ZoneGroupCard's fuller per-room display, which
// needs real width to read well and is the exact thing that's out of
// room here.
Item {
    id: root

    property var selectedZone: null
    signal zoneSelected(var zone)
    implicitHeight: 44

    // Same accent-color computation as ZoneGroupCard.qml's own selected
    // card (this bar only ever represents the *selected* zone, so
    // there's no unselected state to branch on the way that card does)
    // -- colorised to match it, per its own class comment, rather than
    // staying neutral while the card it stands in for goes accent-colored.
    readonly property bool hasAccent: !!root.selectedZone && root.selectedZone.accentColor.a > 0
    readonly property color barBackground: hasAccent ? root.selectedZone.accentColor : "#F5F5F5"
    readonly property bool backgroundIsLight: luminance(barBackground) > 0.55
    readonly property color contrastColor: backgroundIsLight ? "#212121" : "white"

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
        ? blendToward(barBackground, Qt.rgba(0, 0, 0, 1), 0.08)
        : blendToward(barBackground, Qt.rgba(1, 1, 1, 1), 0.12)

    RowLayout {
        Layout.fillWidth: true
        anchors.fill: parent



        Rectangle {
            id: bar
            width: parent.width
            //Layout.fillWidth: true
            anchors.fill: parent
            anchors.rightMargin: 42 // space for cog
            radius: 16//height / 2
            color: mouseArea.containsMouse ? root.hoverColor : root.barBackground

            Behavior on color {
                ColorAnimation { duration: 400 }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 8

                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    // Solid contrastColor while accent-colored, same as
                    // ZoneGroupCard's own dot -- otherwise its usual
                    // ready/not-ready meaning.
                    color: root.hasAccent
                           ? root.contrastColor
                           : (root.selectedZone && root.selectedZone.ready ? "#4CAF50" : "#BDBDBD")
                }

                Label {
                    Layout.fillWidth: true
                    text: root.selectedZone ? root.selectedZone.roomName : qsTr("No zone selected")
                    font.pixelSize: 14
                    font.bold: true
                    color: root.contrastColor
                    elide: Text.ElideRight
                }

                // A Text glyph, not an Image -- there's no chevron_light.svg
                // variant to switch to on a dark accent background, and
                // recoloring an SVG at runtime needs a mask/colorize effect
                // that's already been found unreliable in this Qt build (see
                // MusicServiceRow.qml) -- same reasoning TransportIconButton.qml
                // falls back to a Unicode glyph for shuffle/repeat.
                Text {
                    text: "⌄"
                    font.pixelSize: 16
                    color: root.contrastColor
                }
            }

            MouseArea {
                id: mouseArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: picker.open()
            }
        }

        Popup {
            id: picker
            y: -height - 8
            width: 240
            padding: 6
            modal: true
            focus: true
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

            // Same hand-rolled-dim reasoning as ActionMenu.qml -- suppress
            // this Popup's own default modal dim so it doesn't double up
            // with Main.qml's overlayDim.
            Overlay.modal: Item {}

            enter: Transition {
                NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: 180; easing.type: Easing.OutQuad }
            }
            exit: Transition {
                NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: 140; easing.type: Easing.InQuad }
            }

            background: Rectangle {
                radius: 12
                color: "white"

                layer.enabled: true
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: "#33000000"
                    shadowBlur: 0.6
                    shadowVerticalOffset: 4
                }
            }

            contentItem: ListView {
                implicitHeight: Math.min(280, contentHeight)
                clip: true
                model: groupsModel
                spacing: 2

                delegate: Rectangle {
                    id: row
                    // model.coordinator/model.members, not modelData -- same
                    // named-role access as Main.qml's own zoneListView
                    // delegate; groupsModel is a C++ model exposing named
                    // roles, not a plain array a generic modelData would fit.
                    required property var coordinator

                    width: ListView.view.width
                    height: 36
                    radius: 8
                    color: rowMouseArea.pressed
                        ? "#D0D0D0"
                        : (rowMouseArea.containsMouse ? "#F0F0F0" : "transparent")

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8

                        Rectangle {
                            width: 8
                            height: 8
                            radius: 4
                            color: row.coordinator && row.coordinator.ready ? "#4CAF50" : "#BDBDBD"
                        }

                        Label {
                            Layout.fillWidth: true
                            text: row.coordinator ? row.coordinator.roomName : ""
                            font.pixelSize: 13
                            color: "#212121"
                            elide: Text.ElideRight
                        }
                    }

                    MouseArea {
                        id: rowMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.zoneSelected(row.coordinator)
                            picker.close()
                        }
                    }
                }
            }
        }

        IconButton {
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
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
            items: [qsTr("Mute All")]
        }

    }

    layer.enabled: true
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowColor: "#22000000"
        shadowBlur: 0.5
        shadowVerticalOffset: 2
    }
}
