import QtQuick
import QtQuick.Layouts

// One row in the Browse panel's navigation stack (BrowseStack.qml/
// BrowseListPage.qml): icon + name + a chevron affordance for rows that
// browse into a folder. showChevron is false for playable leaf items,
// which have nothing further to navigate into.
Item {
    id: root

    property string title: ""
    property string imageUrl: ""
    property bool showChevron: true
    // Circular is right for the root services list (small brand logos --
    // AUPEO!, Spotify, ...); once browsing into a service's own folders/
    // tracks, album art and playlist covers read better at their natural
    // rectangular shape instead of being cropped into a circle.
    property bool circularIcon: true
    // Off for the root services list (selecting a service isn't something
    // you queue/replace-queue) -- Browse/search result rows turn it on.
    property bool showMenu: false
    // Off for folders/containers (clicking one navigates, it doesn't
    // "play") -- BrowseListPage.qml turns it on only for playable leaf rows.
    property bool showPlayOverlay: false
    property bool menuOpen: false
    // "01"/"02"/... shown before the icon -- empty (the default) shows
    // nothing and reclaims the space. Only set by BrowseListPage.qml when
    // viewing an actual album/playlist (root.hasFolderArt), not ordinary
    // browsing/search results, where a list position isn't a track number.
    property string trackNumber: ""

    signal clicked()
    signal menuRequested()

    implicitHeight: 52
    readonly property bool rowHoverActive: mouseArea.containsMouse && !rowMenuButton.hovered && !root.menuOpen
    readonly property bool rowPressActive: rowHoverActive && mouseArea.pressed

    // Same gutter geometry as QueuePanel.qml: the highlight extends into
    // the panel padding without moving row contents, while leaving enough
    // space for rounded corners to remain visible. BrowseListPage gives
    // the StackView 5px extra horizontal room so this overhang is not
    // clipped by BrowseStack's slide-transition clip.
    Rectangle {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        x: 0
        width: parent.width + 16
        radius: 10
        color: root.rowPressActive ? "#E8E8E8" : (root.rowHoverActive ? "#F5F5F5" : "transparent")
    }

    // Declared before the RowLayout so the row-wide click target sits
    // *underneath* the menu IconButton's own MouseArea in paint/hit-test
    // order (later siblings win) -- otherwise this would swallow clicks
    // meant for the dots button instead of opening its menu.
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    RowLayout {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.right: parent.right
        anchors.rightMargin: -11
        spacing: 12

        Text {
            // Reclaims its space entirely when empty rather than leaving a
            // gap -- Layout.preferredWidth collapses to 0 alongside
            // visible:false (an invisible Layout item still reserves its
            // implicitWidth otherwise).
            Layout.preferredWidth: root.trackNumber.length > 0 ? implicitWidth : 0
            Layout.alignment: Qt.AlignVCenter
            visible: root.trackNumber.length > 0
            text: root.trackNumber
            font.pixelSize: 12
            color: "#9E9E9E"
        }

        Item {
            id: iconArea
            Layout.preferredWidth: 40
            Layout.preferredHeight: 40

            Rectangle {
                anchors.fill: parent
                radius: root.circularIcon ? width / 2 : 6
                antialiasing: true
                layer.enabled: true
                layer.samples: 4
                color: "#BDBDBD"
                visible: iconImage.status !== RoundedImage.Ready
            }

            RoundedImage {
                id: iconImage
                anchors.fill: parent
                source: root.imageUrl
                radius: root.circularIcon ? width / 2 : 6
                visible: status === RoundedImage.Ready
            }

            Text {
                anchors.centerIn: parent
                visible: iconImage.status !== RoundedImage.Ready
                text: "♪"
                font.pixelSize: 18
                color: "#7A7A7A"
            }

            // Hover-to-play cue -- a plain darkening rect plus a plain
            // white play.svg centered on top; it matches RoundedImage's
            // own corner radius.
            Rectangle {
                anchors.fill: parent
                radius: root.circularIcon ? width / 2 : 6
                antialiasing: true
                color: "#000000"
                opacity: root.showPlayOverlay && mouseArea.containsMouse ? 0.45 : 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: 120 } }
            }

            Image {
                anchors.centerIn: parent
                source: "../resources/icons/play_light.svg"
                sourceSize.width: 16
                sourceSize.height: 16
                visible: root.showPlayOverlay && mouseArea.containsMouse
            }
        }

        Text {
            Layout.fillWidth: true
            text: root.title
            font.pixelSize: Math.round(15 * UiScale.factor)
            font.weight: Typography.emphasisWeight
            color: "#212121"
            elide: Text.ElideRight
        }

        IconButton {
            id: rowMenuButton
            // Only while this row itself is hovered, not permanently --
            // an ActionMenu that's already open (rowMenu.parent set to
            // this row) can keep the button's own hover briefly true after
            // the mouse leaves, which is fine: closing the menu re-hides it.
            visible: root.showMenu && (mouseArea.containsMouse || hovered || root.menuOpen)
            iconSource: "../resources/icons/three_dots_vertical.svg"
            iconSize: 16
            onClicked: root.menuRequested()
        }

        // Rightmost -- the chevron (a folder's "browse into this" cue)
        // sits to its right above, in normal RowLayout child order.
        Image {
            visible: root.showChevron
            source: "../resources/icons/chevron_right.svg"
            sourceSize.width: 20
            sourceSize.height: 20
            opacity: 0.5
        }
    }
}
