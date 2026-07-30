import QtQuick
import QtQuick.Layouts

// Shuffle/previous/play-pause/next/repeat row -- shared between
// NowPlayingWide.qml and NowPlayingCompact.qml, and also instantiated
// invisibly by NowPlayingPanel.qml purely to measure its own natural
// (implicitWidth) footprint at sizeScale 1.0, which is what decides when
// to switch layouts (see NowPlayingPanel.qml's useCompactLayout). Every
// icon here is a fixed size regardless of track/zone data (scaled by
// `sizeScale`, but that's driven by available width, not track/zone
// data), so implicitWidth at a given sizeScale is always the same real
// number -- nothing here needs to actually be visible for that
// measurement to be accurate.
RowLayout {
    id: root

    property var zone // ZonePlayer* -- null when no zone is selected
    property bool isPlaying: false
    property bool backgroundIsLight: true
    property color buttonFillColor: "#212121"
    property color buttonIconColor: "white"
    property color controlHoverColor: Qt.rgba(0, 0, 0, 0.1)
    property color controlPressedColor: Qt.rgba(0, 0, 0, 0.2)
    // A plain JS function reference (root.blendToward on
    // NowPlayingPanel.qml) rather than duplicating the same blend math
    // here -- passed straight through since QML properties can hold a
    // function value.
    property var blendToward

    // Named sizeScale, not scale -- RowLayout (via Item) already has a
    // built-in "scale" property (a visual zoom transform, unrelated to
    // this), so declaring another property with that exact name would
    // collide with it.
    //
    // 1.0 (NowPlayingWide.qml's size, and the only size the wide/compact
    // switch threshold is measured against) everywhere except
    // NowPlayingCompact.qml, which shrinks this to fit whatever width the
    // panel actually has -- reusing this exact row at full size is *why*
    // switching to Compact didn't actually avoid clipping the controls on
    // their own: Compact only ever gets shown once the panel is already
    // narrower than this row needs at sizeScale 1.0 (see
    // NowPlayingPanel.qml's useCompactLayout), so without shrinking here
    // too, Compact hit the exact same overflow Wide was just switched
    // away from. Confirmed via a debug build -- the "repeat" button was
    // clipped off the right edge in Compact at every width the switch
    // threshold allows.
    property real sizeScale: 1.0

    readonly property int buttonSize: Math.round(44 * sizeScale)
    readonly property int playButtonSize: Math.round(56 * sizeScale)

    spacing: Math.round(28 * sizeScale)

    TransportIconButton {
        buttonSize: root.buttonSize
        iconSource: {
            const active = root.zone && root.zone.shuffleEnabled
            if (active)
                return root.backgroundIsLight ? "../resources/icons/shuffle_light.svg" : "../resources/icons/shuffle.svg"
            return root.backgroundIsLight ? "../resources/icons/shuffle.svg" : "../resources/icons/shuffle_light.svg"
        }
        iconSize: Math.round(22 * root.sizeScale)
        enabled: root.zone !== null
        hoverColor: root.controlHoverColor
        pressedColor: root.controlPressedColor
        checked: root.zone && root.zone.shuffleEnabled
        checkedColor: root.buttonFillColor
        onClicked: root.zone.setShuffleEnabled(!root.zone.shuffleEnabled)
    }

    TransportIconButton {
        // Pick whichever pre-baked fill variant reads against the panel
        // background -- recoloring the SVG at runtime would need a
        // mask/colorize effect, and MultiEffect masking was already found
        // unreliable in this Qt build (see MusicServiceRow.qml).
        buttonSize: root.buttonSize
        iconSource: root.backgroundIsLight
            ? "../resources/icons/skip_previous.svg"
            : "../resources/icons/skip_previous_light.svg"
        iconSize: Math.round(28 * root.sizeScale)
        enabled: root.zone !== null
        hoverColor: root.controlHoverColor
        pressedColor: root.controlPressedColor
        onClicked: root.zone.previous()
    }

    Rectangle {
        Layout.preferredWidth: root.playButtonSize
        Layout.preferredHeight: root.playButtonSize
        radius: width / 2
        antialiasing: true
        color: {
            if (!playMouseArea.containsMouse)
                return root.buttonFillColor
            const amount = playMouseArea.pressed ? 0.5 : 0.3
            return root.blendToward(root.buttonFillColor, Qt.rgba(0.5, 0.5, 0.5, 1), amount)
        }

        Image {
            anchors.centerIn: parent
            // buttonIconColor is contrastColor's *inverse* (this icon
            // sits on the button's fill, which is contrastColor, not
            // directly on the panel background) -- so the light/dark
            // variant choice here is the opposite of every other icon in
            // this row.
            source: {
                const name = root.isPlaying ? "pause" : "play"
                return root.backgroundIsLight
                    ? "../resources/icons/" + name + "_light.svg"
                    : "../resources/icons/" + name + ".svg"
            }
            sourceSize.width: Math.round(26 * root.sizeScale)
            sourceSize.height: Math.round(26 * root.sizeScale)
            smooth: true
        }

        MouseArea {
            id: playMouseArea
            anchors.fill: parent
            hoverEnabled: true
            enabled: root.zone !== null
            cursorShape: Qt.PointingHandCursor
            onClicked: root.isPlaying ? root.zone.pause() : root.zone.play()
        }
    }

    TransportIconButton {
        buttonSize: root.buttonSize
        iconSource: root.backgroundIsLight
            ? "../resources/icons/skip_next.svg"
            : "../resources/icons/skip_next_light.svg"
        iconSize: Math.round(28 * root.sizeScale)
        enabled: root.zone !== null
        hoverColor: root.controlHoverColor
        pressedColor: root.controlPressedColor
        onClicked: root.zone.next()
    }

    TransportIconButton {
        buttonSize: root.buttonSize
        iconSource: {
            const active = root.zone && root.zone.repeatMode > 0
            if (active)
                return root.backgroundIsLight ? "../resources/icons/repeat_light.svg" : "../resources/icons/repeat.svg"
            return root.backgroundIsLight ? "../resources/icons/repeat.svg" : "../resources/icons/repeat_light.svg"
        }
        iconSize: Math.round(24 * root.sizeScale)
        enabled: root.zone !== null
        hoverColor: root.controlHoverColor
        pressedColor: root.controlPressedColor
        checked: root.zone && root.zone.repeatMode > 0
        checkedColor: root.buttonFillColor
        onClicked: root.zone.cycleRepeatMode()
    }
}
