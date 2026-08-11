import QtQuick
import QtQuick.Layouts

// Small reusable circular icon button for the transport row. Renders an SVG
// (iconSource) when given one, otherwise falls back to a Unicode glyph
// (iconText) -- shuffle/repeat still use glyphs (no monochrome emoji-free
// symbol exists for those), skip back/forward use real icon assets.
//
// contrastColor drives the glyph color (Image-mode callers instead pick a
// light/dark asset variant themselves, see NowPlayingPanel.qml) so this
// button reads correctly whether it's sitting on the plain white "minimal"
// background or a per-track accent color of unknown lightness.
Item {
    id: button

    property string iconText: ""
    property int iconPixelSize: 20
    property url iconSource: ""
    property int iconSize: 20
    property color contrastColor: "#424242"
    property color disabledColor: "#D0D0D0"
    // Default is a translucent tint of contrastColor (works anywhere this
    // button sits directly on the panel background); callers whose hover
    // needs to read against a colored background instead -- see
    // NowPlayingPanel.qml's controlHoverColor/controlPressedColor -- pass
    // an already-blended opaque color here instead.
    property color hoverColor: Qt.rgba(contrastColor.r, contrastColor.g, contrastColor.b, 0.10)
    property color pressedColor: Qt.rgba(contrastColor.r, contrastColor.g, contrastColor.b, 0.22)
    property bool checked: false
    property color checkedColor: contrastColor
    property color checkedHoverColor: checkedColor
    property color checkedPressedColor: checkedColor

    signal clicked()

    // Overridable so NowPlayingTransportControls.qml can shrink these
    // circles for NowPlayingCompact.qml (see its own iconScale) without
    // needing a second copy of this whole button.
    property int buttonSize: 44

    // implicitWidth/Height rather than Layout.preferredWidth/Height: Qt
    // Quick Layouts fall back to implicitWidth/Height for their preferred
    // size when Layout.preferredWidth/Height isn't set, so this still sizes
    // correctly inside the transport row's RowLayout -- but unlike
    // Layout.preferredWidth/Height (which only Layout containers apply, so
    // it's silently discarded outside one, collapsing to a 0-size button),
    // implicitWidth/Height sizes the button correctly even when placed
    // directly via anchors, like the corner volume button.
    implicitWidth: buttonSize
    implicitHeight: buttonSize

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        antialiasing: true
        color: {
            if (!button.enabled)
                return "transparent"
            if (button.checked) {
                if (mouseArea.pressed)
                    return button.checkedPressedColor
                return mouseArea.containsMouse ? button.checkedHoverColor : button.checkedColor
            }
            if (!mouseArea.containsMouse)
                return "transparent"
            return mouseArea.pressed ? button.pressedColor : button.hoverColor
        }
    }

    Text {
        anchors.centerIn: parent
        visible: !button.iconSource.toString()
        text: button.iconText
        font.pixelSize: button.iconPixelSize
        color: button.enabled ? button.contrastColor : button.disabledColor
    }

    Image {
        anchors.centerIn: parent
        visible: !!button.iconSource.toString()
        source: button.iconSource
        sourceSize.width: button.iconSize
        sourceSize.height: button.iconSize
        smooth: true
        // The SVG's own fill is baked in (caller picks the light/dark
        // asset variant); dim it for disabled via opacity instead of
        // recoloring, since that doesn't need any shader/mask effect to
        // work reliably.
        opacity: button.enabled ? 1.0 : 0.4
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        enabled: button.enabled
        cursorShape: Qt.PointingHandCursor
        onClicked: button.clicked()
    }
}
