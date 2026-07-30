import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts

// Scrollable "Up Next" queue list for the currently selected zone/group.
Item {
    id: root

    property var queue // QueueModel* for the selected zone
    property var failedImageUrls: ({})

    function imageUrlAvailable(url) {
        return !!url && !failedImageUrls[String(url)]
    }

    function markImageUrlFailed(url) {
        if (!url)
            return

        var failed = Object.assign({}, failedImageUrls)
        failed[String(url)] = true
        failedImageUrls = failed
    }

    // Shared by every row rather than one per delegate -- opened against
    // whichever row's dots button was clicked (see the delegate below),
    // same approach as Main.qml's zonesSettingsMenu.
    ActionMenu {
        id: rowMenu
        property int trackIndex: -1
        property string trackId: ""
        property var currentItem: ({})
        items: [qsTr("Favourite"), "-", qsTr("Play Now"), qsTr("Remove Track")]

        onItemClicked: (text) => {
            if (!root.queue || !root.queue.zone)
                return
            if (text === qsTr("Play Now"))
                root.queue.zone.playQueueItem(rowMenu.trackIndex + 1, rowMenu.currentItem)
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
                font.weight: Typography.emphasisWeight
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
            Layout.leftMargin: -5
            Layout.rightMargin: -5
            clip: true
            spacing: 4
            model: root.queue

            delegate: Item {
                id: rowItem
                width: listView.width - 16
                height: 56
                readonly property var currentTrack:
                    root.queue && root.queue.zone ? root.queue.zone.currentTrack : null
                readonly property string rowTrackId: model.id ? String(model.id) : ""
                readonly property string rowTrackUri: model.uri ? String(model.uri) : ""
                readonly property string currentTrackId: currentTrack && currentTrack.id ? String(currentTrack.id) : ""
                readonly property string currentTrackUri: currentTrack && currentTrack.uri ? String(currentTrack.uri) : ""
                readonly property bool isCurrentQueueTrack:
                    (!!rowTrackUri && rowTrackUri === currentTrackUri)
                    || queueIdMatches(rowTrackId, currentTrackId)
                readonly property bool rowHoverActive: mouseArea.containsMouse && !rowMenuButton.hovered

                function queueIdMatches(rowId, trackId) {
                    if (!rowId || !trackId)
                        return false
                    return trackId === rowId
                        || trackId.indexOf(rowId + "/") === 0
                        || rowId.indexOf(trackId + "/") === 0
                }

                // Bleeds into the panel gutter without moving row content.
                // Plain x/width mirrors MusicServiceRow.qml's workaround for
                // conditional overhang geometry.
                Rectangle {
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    x: 0
                    width: parent.width + 16
                    radius: 10
                    antialiasing: true
                    color: rowItem.rowHoverActive
                           ? "#F5F5F5"
                           : (rowItem.isCurrentQueueTrack && !rowMenuButton.hovered ? "#E0E0E0" : "transparent")
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
                            root.queue.zone.playQueueItem(index + 1, model.item)
                    }
                }

                RowLayout {
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.right: parent.right
                    anchors.rightMargin: -11
                    spacing: 12

                    Item {
                        Layout.preferredWidth: 44
                        Layout.preferredHeight: 44

                        Rectangle {
                            anchors.fill: parent
                            radius: 6
                            antialiasing: true
                            layer.enabled: true
                            layer.samples: 4
                            color: "#BDBDBD"
                            visible: artImage.status !== RoundedImage.Ready
                        }

                        RoundedImage {
                            id: artImage
                            anchors.fill: parent
                            readonly property string requestedUrl: model.imageUrl ? String(model.imageUrl) : ""
                            source: root.imageUrlAvailable(requestedUrl) ? requestedUrl : ""
                            radius: 6
                            visible: status === RoundedImage.Ready
                            onStatusChanged: {
                                if (status === RoundedImage.Error)
                                    root.markImageUrlFailed(requestedUrl)
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: artImage.status !== RoundedImage.Ready
                            text: "♪"
                            font.pixelSize: 16
                            color: "#7A7A7A"
                        }

                        // Hover-to-play cue -- same darkening-rect-plus-
                        // white-icon approach as MusicServiceRow.qml's
                        // showPlayOverlay (every queue row is already a
                        // playable track, so it's unconditional here).
                        Rectangle {
                            anchors.fill: parent
                            color: "#000000"
                            opacity: rowItem.rowHoverActive ? 0.45 : 0
                            visible: opacity > 0
                            Behavior on opacity { NumberAnimation { duration: 120 } }
                        }

                        Image {
                            anchors.centerIn: parent
                            source: "../resources/icons/play_light.svg"
                            sourceSize.width: 16
                            sourceSize.height: 16
                            visible: rowItem.rowHoverActive
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
                            font.weight: Typography.emphasisWeight
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
                        id: rowMenuButton
                        iconSource: "../resources/icons/three_dots_vertical.svg"
                        iconSize: 16
                        onClicked: {
                            rowMenu.trackIndex = index
                            rowMenu.trackId = model.id
                            rowMenu.currentItem = model.item
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
