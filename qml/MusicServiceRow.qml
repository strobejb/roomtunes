import QtQuick
import QtQuick.Layouts
import QtQuick.Effects

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
    // "01"/"02"/... shown before the icon -- empty (the default) shows
    // nothing and reclaims the space. Only set by BrowseListPage.qml when
    // viewing an actual album/playlist (root.hasFolderArt), not ordinary
    // browsing/search results, where a list position isn't a track number.
    property string trackNumber: ""

    signal clicked()
    signal menuRequested()

    implicitHeight: 44

    // Bleeds a little into the surrounding panel margin (the "gutter")
    // rather than stopping flush at the row's own edges -- purely a
    // decorative overhang on the highlight itself, the row (and the
    // ListView/panel it sits in) keeps its actual width unchanged. Left
    // edge starts at the album art itself (not the row's own left edge)
    // when a track number is showing, so the highlight doesn't cover it --
    // same -10 bleed either way, just measured from a different edge.
    //
    // Plain x/width, not anchors.left/anchors.right + margins -- confirmed
    // via a debug build (on-screen geometry readout) that mixing an
    // anchors.left bound to a conditional target (iconArea.left vs
    // parent.left) with an anchors.right bound to parent.right collapsed
    // this Rectangle to width:0 in this Qt version, even though both edges
    // individually resolved to sane values. Equivalent plain x/width
    // bindings don't have that problem.
    Rectangle {
        readonly property real leftEdge: (root.trackNumber.length > 0 ? iconArea.x : 0) - 10
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        x: leftEdge
        width: parent.width - leftEdge + 10
        radius: 10
        color: mouseArea.containsMouse ? "#F5F5F5" : "transparent"
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
        anchors.fill: parent
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
                color: "#BDBDBD"
                visible: iconImage.status !== Image.Ready
            }

            // The actual icon, never shown directly -- MultiEffect below
            // reads its pixels through `source:` and applies the mask, so
            // this can stay full-quality (sourceSize/smooth/mipmap) without
            // needing to be clipped itself.
            Image {
                id: iconImage
                anchors.fill: parent
                source: root.imageUrl
                sourceSize.width: width
                sourceSize.height: height
                smooth: true
                mipmap: true
                visible: false
            }

            // Circle/rounded-rect shape used purely as a mask -- its own
            // fill color is irrelevant, MultiEffect reads its alpha.
            Item {
                id: maskShape
                anchors.fill: parent
                layer.enabled: true
                visible: false

                Rectangle {
                    anchors.fill: parent
                    radius: root.circularIcon ? width / 2 : 6
                    color: "black"
                }
            }

            // Replaced an earlier Canvas 2D (ctx.drawImage + ctx.clip)
            // approach -- confirmed via an isolated test (qml.exe, a real
            // Sonos icon URL, side-by-side against a plain unclipped Image)
            // that Canvas's own drawImage() renders visibly softer than a
            // plain Image at the exact same source/size regardless of
            // sourceSize/smooth/mipmap/imageSmoothingQuality settings --
            // the degradation was in Canvas's rendering itself, not the
            // decode. MultiEffect's maskSource does work correctly in this
            // Qt build (retested directly); an earlier comment here
            // claiming it rendered nothing was either a stale finding or
            // testing a different failure mode -- worth knowing if masking
            // ever seems broken again elsewhere in this app.
            MultiEffect {
                anchors.fill: parent
                visible: iconImage.status === Image.Ready
                source: iconImage
                maskEnabled: true
                maskSource: maskShape
            }

            Text {
                anchors.centerIn: parent
                visible: iconImage.status !== Image.Ready
                text: "♪"
                font.pixelSize: 18
                color: "#7A7A7A"
            }

            // Hover-to-play cue -- a plain darkening rect (not a mask) plus
            // a plain white play.svg centered on top; nothing here needs
            // clipping to the art's own shape beyond matching its corner
            // radius, so there's no need for the MultiEffect masking
            // approach above.
            Rectangle {
                anchors.fill: parent
                radius: root.circularIcon ? width / 2 : 6
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
            // Only while this row itself is hovered, not permanently --
            // an ActionMenu that's already open (rowMenu.parent set to
            // this row) can keep the button's own hover briefly true after
            // the mouse leaves, which is fine: closing the menu re-hides it.
            visible: root.showMenu && mouseArea.containsMouse
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
