import QtQuick
import QtQuick.Effects

// Reusable white, rounded-corner panel with a soft shadow, sitting on the
// app's light-grey background.
Rectangle {
    id: panel

    color: "white"
    radius: 16

    // Harmless for panels whose color never changes; lets the Now Playing
    // panel's per-track accent color (see NowPlayingPanel.qml) fade in
    // instead of jumping.
    Behavior on color {
        ColorAnimation { duration: 400 }
    }

    layer.enabled: true
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowColor: "#22000000"
        shadowBlur: 0.5
        shadowVerticalOffset: 2
    }
}
