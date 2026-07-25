import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

// DeviceLink/AppLink sign-in flow for a MusicService that needsSignIn --
// see SmapiService::beginSignIn()/completeSignIn(). Styled to match
// ActionMenu.qml's white rounded-corner-plus-shadow popup look. Opened
// from BrowseListPage.qml's "Sign in required" empty state.
Popup {
    id: dialog

    property var service

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: 320
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 20

    property string linkCode: ""
    property string regUrl: ""
    property string errorMessage: ""
    property bool waiting: true

    onOpened: {
        linkCode = ""
        regUrl = ""
        errorMessage = ""
        waiting = true
        if (service)
            service.beginSignIn()
    }

    Connections {
        target: dialog.service
        // service isn't always a SmapiService (e.g. the Sonos Music
        // Library never needs sign-in, so this dialog is instantiated for
        // it too but never opened) -- suppress the "no signal matches"
        // warning for a target that legitimately doesn't have these.
        ignoreUnknownSignals: true

        function onDeviceLinkCodeReady(code, url) {
            dialog.linkCode = code
            dialog.regUrl = url
            dialog.waiting = false
        }

        function onAuthorized() {
            dialog.close()
        }

        function onAuthorizationFailed(message) {
            dialog.errorMessage = message
            dialog.waiting = false
        }
    }

    background: Rectangle {
        radius: 10
        color: "white"

        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: "#33000000"
            shadowBlur: 0.6
            shadowVerticalOffset: 4
        }
    }

    contentItem: ColumnLayout {
        spacing: 12

        Label {
            Layout.fillWidth: true
            text: qsTr("Sign in to %1").arg(dialog.service ? dialog.service.title : "")
            font.pixelSize: 15
            font.bold: true
            color: "#212121"
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            visible: dialog.waiting && !dialog.errorMessage
            text: qsTr("Please wait…")
            color: "#9E9E9E"
            font.pixelSize: 13
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            visible: !dialog.waiting && !dialog.errorMessage

            Label {
                Layout.fillWidth: true
                text: qsTr("Go to %1 and enter this code:").arg(dialog.regUrl)
                wrapMode: Text.WordWrap
                font.pixelSize: 13
                color: "#212121"
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: dialog.linkCode
                font.pixelSize: 22
                font.bold: true
                color: "#212121"
            }

            Button {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("I've signed in")
                onClicked: {
                    dialog.waiting = true
                    dialog.service.completeSignIn(dialog.linkCode)
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: !!dialog.errorMessage
            text: dialog.errorMessage
            color: "#D32F2F"
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }

        Button {
            Layout.alignment: Qt.AlignHCenter
            visible: !!dialog.errorMessage
            text: qsTr("Retry")
            onClicked: {
                dialog.errorMessage = ""
                dialog.waiting = true
                dialog.service.beginSignIn()
            }
        }
    }
}
