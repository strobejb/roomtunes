import QtQuick
import QtQuick.Layouts

Item {
    id: root

    property var zone: null
    property var track: null
    property bool backgroundIsLight: true
    property color controlHoverColor: Qt.rgba(0, 0, 0, 0.1)
    property color controlPressedColor: Qt.rgba(0, 0, 0, 0.2)
    property bool volumeExpanded: false

    function musicServiceName() {
        const uri = track && track.uri ? String(track.uri).toLowerCase() : ""
        const id = track && track.id ? String(track.id).toLowerCase() : ""
        const text = uri + " " + id
        if (text.indexOf("spotify") >= 0)
            return qsTr("Spotify")
        if (text.indexOf("amazon") >= 0)
            return qsTr("Amazon Music")
        if (text.indexOf("bbc") >= 0)
            return qsTr("BBC Sounds")
        if (text.indexOf("tunein") >= 0 || text.indexOf("radiotime") >= 0)
            return qsTr("TuneIn")
        if (text.indexOf("sonos") >= 0)
            return qsTr("Sonos")
        return qsTr("Music Service")
    }

    width: actionRow.implicitWidth
    height: 44
    visible: root.zone !== null
    opacity: root.volumeExpanded ? 0 : 1
    enabled: !root.volumeExpanded

    Behavior on opacity {
        NumberAnimation { duration: 120; easing.type: Easing.OutQuad }
    }

    NowPlayingContextMenu {
        id: nowPlayingMenu
        zone: root.zone
        track: root.track
        musicServiceName: root.musicServiceName()
    }

    RowLayout {
        id: actionRow
        anchors.fill: parent
        spacing: 4

        TransportIconButton {
            Layout.preferredWidth: 44
            Layout.preferredHeight: 44
            buttonSize: 44
            iconSource: root.backgroundIsLight ? "../resources/icons/heart.svg" : "../resources/icons/heart_light.svg"
            iconSize: 19
            hoverColor: root.controlHoverColor
            pressedColor: root.controlPressedColor
            enabled: root.track !== null
            onClicked: {
                if (root.zone)
                    root.zone.addCurrentTrackToSonosFavourites()
            }
        }

        TransportIconButton {
            id: menuButton
            Layout.preferredWidth: 44
            Layout.preferredHeight: 44
            buttonSize: 44
            iconSource: root.backgroundIsLight ? "../resources/icons/three_dots.svg" : "../resources/icons/three_dots_light.svg"
            iconSize: 16
            hoverColor: root.controlHoverColor
            pressedColor: root.controlPressedColor
            enabled: root.track !== null
            onClicked: nowPlayingMenu.openFor(menuButton)
        }
    }
}
