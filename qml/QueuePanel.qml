import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Scrollable "Up Next" queue list for the currently selected zone/group.
Item {
    id: root

    property var queue // QueueModel* for the selected zone

    // Shared by every row rather than one per delegate -- opened against
    // whichever row's dots button was clicked (see the delegate below),
    // same approach as Main.qml's zonesSettingsMenu.
    ActionMenu {
        id: rowMenu
        property int trackIndex: -1
        property string trackId: ""
        items: [qsTr("Favourite"), "-", qsTr("Play Now"), qsTr("Remove Track")]

        onItemClicked: (text) => {
            if (!root.queue || !root.queue.zone)
                return
            if (text === qsTr("Play Now"))
                root.queue.zone.playQueueTrack(rowMenu.trackIndex + 1)
            else if (text === qsTr("Remove Track"))
                root.queue.zone.removeQueueTrack(rowMenu.trackId)
            // "Favourite" isn't wired to anything yet -- there's no
            // favourites feature in this app yet (AddItemToFavorites/a
            // favourites list) to call into, same "visual affordance only"
            // situation as the Edit pill below.
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: qsTr("Queue")
                font.pixelSize: Math.round(18 * UiScale.factor)
                font.bold: true
                color: "#212121"
            }

            // Not wired to any editing functionality yet -- a visual
            // affordance only, same as the Browse panel's search icon.
            // Unlike those (transparent until hovered), these two sit with
            // a persistently light-grey background for contrast against
            // the white panel, darkening further on hover.
            PillIconButton {
                iconSource: "../resources/icons/queue_play.svg"
                label: qsTr("Edit")
                idleColor: "#E8E8E8"
                hoverColor: "#D0D0D0"
                pressedColor: "#B8B8B8"
            }

            IconButton {
                iconSource: "../resources/icons/three_dots.svg"
                iconSize: 16
                idleColor: "#E8E8E8"
                hoverColor: "#D0D0D0"
                pressedColor: "#B8B8B8"
            }
        }

        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: root.queue

            delegate: Item {
                id: rowItem
                width: listView.width - 16
                height: 56

                // Same bleed-into-the-margin hover highlight as
                // MusicServiceRow.qml's Browse/search rows.
                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: -10
                    anchors.rightMargin: -10
                    radius: 10
                    color: mouseArea.containsMouse ? "#F5F5F5" : "transparent"
                }

                // Already-queued track -- Seek(TRACK_NR) + Play, no
                // AddURIToQueue involved (roomtunes-bb10's skipto_track()).
                // index is 0-based; queue track numbers are 1-based.
                // Declared before the RowLayout so the row-wide click
                // target sits underneath the menu IconButton's own
                // MouseArea in hit-test order (later siblings win) --
                // same reasoning as MusicServiceRow.qml.
                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (root.queue && root.queue.zone)
                            root.queue.zone.playQueueTrack(index + 1)
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    spacing: 12

                    Rectangle {
                        Layout.preferredWidth: 44
                        Layout.preferredHeight: 44
                        radius: 6
                        color: "#E8E8E8"
                        clip: true

                        Image {
                            id: artImage
                            anchors.fill: parent
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            source: model.imageUrl ? model.imageUrl : ""
                            visible: status === Image.Ready
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: artImage.status !== Image.Ready
                            text: "♪"
                            font.pixelSize: 16
                            color: "#BDBDBD"
                        }

                        // Hover-to-play cue -- same darkening-rect-plus-
                        // white-icon approach as MusicServiceRow.qml's
                        // showPlayOverlay (every queue row is already a
                        // playable track, so it's unconditional here).
                        Rectangle {
                            anchors.fill: parent
                            color: "#000000"
                            opacity: mouseArea.containsMouse ? 0.45 : 0
                            visible: opacity > 0
                            Behavior on opacity { NumberAnimation { duration: 120 } }
                        }

                        Image {
                            anchors.centerIn: parent
                            source: "../resources/icons/play_light.svg"
                            sourceSize.width: 16
                            sourceSize.height: 16
                            visible: mouseArea.containsMouse
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 2

                        Text {
                            Layout.fillWidth: true
                            text: model.title
                            font.pixelSize: Math.round(15 * UiScale.factor)
                            font.bold: true
                            color: "#212121"
                            elide: Text.ElideRight
                        }

                        Text {
                            Layout.fillWidth: true
                            text: model.artist
                            font.pixelSize: 12
                            color: "#757575"
                            elide: Text.ElideRight
                        }
                    }

                    IconButton {
                        iconSource: "../resources/icons/three_dots_vertical.svg"
                        iconSize: 16
                        onClicked: {
                            rowMenu.trackIndex = index
                            rowMenu.trackId = model.id
                            rowMenu.parent = rowItem
                            rowMenu.x = rowItem.width - rowMenu.width - 8
                            rowMenu.y = rowItem.height
                            rowMenu.open()
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: listView.count === 0
                text: qsTr("Queue is empty")
                color: "#9E9E9E"
                font.pixelSize: 13
            }
        }
    }
}
