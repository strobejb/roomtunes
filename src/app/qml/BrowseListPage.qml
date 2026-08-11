import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts

// One level of the Browse panel's navigation stack: a browse() or search()
// result for one folder of one MusicService (the Sonos Music Library or a
// SMAPI partner service -- see src/core/services/MusicService.h). Only ever
// created by BrowseStack.qml pushing pageComponent (either the initial
// "root" folder of a service, or -- recursively, from this same file -- a
// sub-folder browsed into from an existing page, or a search-results page,
// which reuses this exact component with searchTerm set instead of a
// plain objectId), never instantiated standalone.
Item {
    id: root

    property StackView stack
    property Component pageComponent

    property string title: ""
    property var service
    property string objectId: "root"
    // The full browse/search item that was clicked to reach this page (set
    // only by pushFolder() call sites below -- see MusicServiceRow.qml's
    // onClicked) -- carries the imageUrl/artist the folder-art header below
    // needs, plus everything ZonePlayer::playItem() needs for the header's
    // own Play pill to play this whole folder/album.
    property var folderItem: ({})
    property var browseItem: ({})
    // Configurable, defaults on: shows a square folder/album art image at
    // the top of the page (with title/artist/Play pill underneath) instead
    // of going straight to the results list. Off for the root services
    // list, a fresh search-results page, or a folder with no art to show --
    // see hasFolderArt.
    property bool showFolderArt: true
    // Only for an actual album, artist, or playlist -- not a generic
    // section folder (a service's "Top 10s"/"New Releases", a plain
    // "Playlists" bucket, ...), which has nothing coherent to show as "the
    // art for this folder". SMAPI items carry their own precise itemType
    // (album/albumList/artist/playlist/container/other/...); that's
    // authoritative when present since SmapiService's lookupUpnpClass()
    // collapses several different itemTypes -- including generic
    // "container"/"other" sections -- into the same musicAlbum upnpClass,
    // too lossy to tell apart on its own. Library items never set
    // itemType, so fall back to their real DIDL upnp:class there instead.
    readonly property bool folderIsAlbumArtistOrPlaylist: {
        const type = root.folderItem.itemType || ""
        if (type.length > 0)
            return type === "album" || type === "albumList" || type === "artist" || type === "playlist"
        const cls = root.folderItem.upnpClass || ""
        return cls.indexOf("musicAlbum") >= 0 || cls.indexOf("musicArtist") >= 0 || cls.indexOf("playlistContainer") >= 0
    }
    readonly property bool hasFolderArt: root.showFolderArt && !root.isSearchResults
                                          && root.objectId !== "root" && !!root.folderItem.imageUrl
                                          && root.folderIsAlbumArtistOrPlaylist
    // The ZonePlayer a leaf-item click plays on -- see BrowseStack.qml's
    // "zone: stack.zone" binding on pageComponent, which supplies this for
    // every page (root and recursively-pushed sub-folders/search results)
    // without it needing to be threaded through pushFolder()/
    // pushSearchResults() properties.
    property var zone

    // Non-empty means this page shows service.search() results rather
    // than service.browse(objectId) -- set only when pushed from the
    // search field below, never toggled in place on an existing page, so
    // a search result is a stacked panel exactly like a browsed sub-folder
    // is, not a mutation of whatever page was already showing.
    property string searchTerm: ""
    readonly property bool isSearchResults: root.searchTerm.length > 0
    readonly property bool isSmapiServiceRootPage: !root.isSearchResults
                                                   && root.objectId === "root"
                                                   && root.service
                                                   && root.service.serviceId >= 0

    property bool loading: true
    property string errorMessage: ""
    property var items: []

    // Computed once at creation and left stable for this page's lifetime --
    // distinguishes this page's in-flight browse()/search() call from any
    // other page's, so a stale reply arriving after the user has already
    // navigated away can't overwrite the wrong page's content.
    readonly property string requestToken: (root.searchTerm ? ("search:" + root.searchTerm) : (root.browseItem.id || objectId)) + "|" + Math.random()

    Component.onCompleted: {
        // A service needing sign-in shows the sign-in prompt instead of
        // attempting to browse -- there's nothing a browse() call could
        // return yet.
        if (service && service.needsSignIn)
            loading = false
        else
            load()
    }

    function load() {
        loading = true
        errorMessage = ""
        items = []
        if (root.searchTerm) {
            // "tracks" is only a starting hint for the very first search on
            // a fresh page -- service.activeSearchCategory (a real,
            // service-specific category id) takes over once resolved, so a
            // page revisited via load() keeps whatever category was last
            // selected instead of resetting to the default every time.
            const category = (service.activeSearchCategory && service.activeSearchCategory.length > 0)
                ? service.activeSearchCategory : "tracks"
            service.search(requestToken, category, root.searchTerm)
        } else if (root.browseItem && Object.keys(root.browseItem).length > 0) {
            service.browseItem(requestToken, root.browseItem)
        } else {
            service.browse(requestToken, objectId)
        }
    }

    function recordFavouriteUse(item) {
        if (!item)
            return

        const itemId = String(item.id || "")
        const parentId = String(item.parentId || "")
        if (root.objectId === "FV:2" || parentId === "FV:2" || itemId.indexOf("FV:2/") === 0)
            browseHistory.recordUse("browse:favourite:" + itemId)
    }

    // Re-runs the same search under a different category -- e.g. the user
    // picked "Albums" instead of "Tracks" from the filter pills below.
    function switchCategory(categoryId) {
        if (!root.isSearchResults || !root.service || categoryId === root.service.activeSearchCategory)
            return
        loading = true
        errorMessage = ""
        items = []
        service.search(requestToken, categoryId, root.searchTerm)
        // Picking a filter pill shouldn't close or unfocus the search box --
        // keep it open with the cursor active, same as right after a fresh
        // search lands.
        searchControl.expanded = true
        searchInput.forceActiveFocus()
    }

    function clearSearchPill() {
        searchInput.text = ""
        searchControl.expanded = false
        searchInput.focus = false
    }

    StackView.onStatusChanged: {
        if (StackView.status !== StackView.Active)
            clearSearchPill()
    }

    Connections {
        target: root.service

        function onBrowseFinished(token, ok, message, results) {
            if (token !== root.requestToken)
                return
            root.loading = false
            if (ok) {
                root.items = results
            } else {
                root.errorMessage = message
                root.items = []
            }
        }

        // Once sign-in completes, load the folder that was waiting on it.
        function onNeedsSignInChanged() {
            if (root.service && !root.service.needsSignIn)
                root.load()
        }
    }

    SignInDialog {
        id: signInDialog
        service: root.service
    }

    // Shared by every row -- opened against whichever row's dots button
    // was clicked (see the delegate below), same approach as
    // QueuePanel.qml's rowMenu/Main.qml's zonesSettingsMenu.
    ActionMenu {
        id: rowMenu
        property var currentItem: ({})
        items: [qsTr("Play Now"), qsTr("Play Next"), qsTr("Add to End of Queue"), qsTr("Replace Queue")]

        onItemClicked: (text) => {
            if (!root.zone || !rowMenu.currentItem.uri)
                return
            root.recordFavouriteUse(rowMenu.currentItem)
            if (text === qsTr("Play Now"))
                root.zone.playItem(rowMenu.currentItem)
            else if (text === qsTr("Play Next"))
                root.zone.playItemNext(rowMenu.currentItem)
            else if (text === qsTr("Add to End of Queue"))
                root.zone.addItemToQueue(rowMenu.currentItem)
            else if (text === qsTr("Replace Queue"))
                root.zone.replaceQueueWithItem(rowMenu.currentItem)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 5
        anchors.rightMargin: 5
        spacing: 8

        Item {
            id: header
            Layout.fillWidth: true
            implicitHeight: 32

            Item {
                id: backButton
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 32
                height: 32

                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: backMouseArea.containsMouse ? "#F0F0F0" : "transparent"
                }

                Image {
                    anchors.centerIn: parent
                    source: "../resources/icons/chevron_left.svg"
                    sourceSize.width: 26
                    sourceSize.height: 26
                }

                MouseArea {
                    id: backMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.stack.goBack()
                }
            }

            Label {
                anchors.left: backButton.right
                anchors.leftMargin: 4
                anchors.right: parent.right
                // Always reserves the search pill's collapsed footprint
                // (see searchControl.reserveWidth) rather than only once
                // it's actually expanded, so opening it never covers a
                // chunk of title that was visible a moment before -- same
                // reasoning as NowPlayingPanel.qml's volume pill reserving
                // root.volumeReserveWidth against its own title Text.
                anchors.rightMargin: searchControl.reserveWidth
                anchors.verticalCenter: parent.verticalCenter
                text: root.title
                font.pixelSize: Math.round(16 * UiScale.factor)
                font.weight: Typography.emphasisWeight
                color: "#212121"
                elide: Text.ElideRight
            }

            // Sits above the header's own left-to-right flow rather than
            // inside a RowLayout, so it can grow *leftward* from a fixed
            // right edge without reflowing anything else -- the same
            // "icon never moves, a pill grows from it" language
            // NowPlayingPanel.qml's volume control uses, just horizontal
            // instead of vertical here since the icon has to stay clear of
            // the title text to its left rather than other controls below.
            // Expanded, it spans the full header width except the back
            // button's own footprint -- wide enough to fully cover the
            // title underneath, which is the point (not just reserve a
            // fixed gap next to it).
            Item {
                id: searchControl
                visible: !!root.service && root.service.canSearch
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter

                // Starts already expanded on a search-results page itself
                // (isSearchResults is fixed for the page's whole lifetime,
                // so this is just its initial state -- later imperative
                // assignments, e.g. the icon click below, still override
                // it as normal) -- landing on a fresh set of results with
                // the box closed would mean re-clicking the icon just to
                // refine the term you already typed to get here.
                property bool expanded: root.isSearchResults
                readonly property int collapsedWidth: 32
                readonly property int reserveWidth: collapsedWidth + 8

                width: expanded ? (parent.width - backButton.width - 4) : collapsedWidth
                height: 32

                Behavior on width {
                    NumberAnimation { duration: 150; easing.type: Easing.OutQuad }
                }

                Rectangle {
                    anchors.fill: parent
                    radius: height / 2
                    antialiasing: true
                    color: searchControl.expanded ? "#F0F0F0" : (iconMouseArea.containsMouse ? "#F0F0F0" : "transparent")
                }

                // Small static icon of the service being searched, sitting
                // left of the typed text.
                Item {
                    id: serviceIcon
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20
                    height: 20
                    visible: searchControl.expanded

                    RoundedImage {
                        id: serviceIconImage
                        anchors.fill: parent
                        source: root.service ? root.service.iconSource : ""
                        radius: width / 2
                        visible: status === RoundedImage.Ready
                    }
                }

                TextField {
                    id: searchInput
                    anchors.left: serviceIcon.right
                    anchors.leftMargin: 8
                    anchors.right: iconArea.left
                    anchors.verticalCenter: parent.verticalCenter
                    visible: searchControl.expanded
                    opacity: searchControl.expanded ? 1 : 0
                    background: Item {}
                    font.pixelSize: 13
                    placeholderText: qsTr("Search")
                    // Pre-filled with the term that got us here on a
                    // search-results page, so a freshly-pushed page reads as
                    // "still searching for this" rather than a blank box --
                    // an initial value only (isSearchResults/searchTerm are
                    // both fixed for the page's lifetime), typing normally
                    // takes over from there.
                    text: root.isSearchResults ? root.searchTerm : ""

                    onAccepted: {
                        const term = text.trim()
                        if (term.length === 0)
                            return

                        root.stack.pushSearchResults(root.pageComponent, {
                            title: term,
                            service: root.service,
                            searchTerm: term,
                            stack: root.stack,
                            pageComponent: root.pageComponent
                        })
                    }

                    Keys.onEscapePressed: (event) => {
                        root.clearSearchPill()
                        event.accepted = true
                    }

                    onActiveFocusChanged: {
                        if (!activeFocus && text.length === 0)
                            searchControl.expanded = false
                    }

                    Component.onCompleted: {
                        // Cursor active and ready to refine the term
                        // immediately, rather than requiring a click first.
                        if (root.isSearchResults) {
                            forceActiveFocus()
                            cursorPosition = text.length
                        }
                    }
                }

                Item {
                    id: iconArea
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 32
                    height: 32

                    Image {
                        anchors.centerIn: parent
                        source: "../resources/icons/search.svg"
                        sourceSize.width: 18
                        sourceSize.height: 18
                    }

                    MouseArea {
                        id: iconMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (!searchControl.expanded) {
                                searchControl.expanded = true
                                searchInput.forceActiveFocus()
                            } else if (searchInput.text.trim().length > 0) {
                                searchInput.accepted()
                            } else {
                                searchControl.expanded = false
                            }
                        }
                    }
                }
            }
        }

        // Filter pills for a search-results page -- which categories a
        // service offers (Spotify: Tracks/Albums/Artists/Playlists, its
        // own ids/titles, not something this app invents) only becomes
        // known once the first search resolves them; see
        // MusicService::searchCategories. Picking a different pill re-runs
        // the same search term under that category instead. Centered
        // (Layout.alignment, not Layout.fillWidth) rather than pinned left.
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 8
            visible: root.isSearchResults && !!root.service && root.service.searchCategories.length > 0

            Repeater {
                model: root.service ? root.service.searchCategories : []

                delegate: Rectangle {
                    id: pill
                    required property var modelData
                    readonly property bool active: root.service && modelData.id === root.service.activeSearchCategory

                    radius: height / 2
                    implicitHeight: 28
                    implicitWidth: pillLabel.implicitWidth + 20
                    color: active ? "#D0D0D0" : (pillMouseArea.containsMouse ? "#E8E8E8" : "#F0F0F0")

                    Label {
                        id: pillLabel
                        anchors.centerIn: parent
                        text: pill.modelData.title
                        font.pixelSize: 12
                        font.weight: pill.active ? Typography.emphasisWeight : Font.Normal
                        color: "#212121"
                    }

                    MouseArea {
                        id: pillMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.switchCategory(pill.modelData.id)
                    }
                }
            }
        }

        // Single flexible region: exactly one of loading/error/sign-in/
        // results is showing at a time, but all four live here, overlaid,
        // always claiming this same fillWidth+fillHeight space -- rather
        // than being flat ColumnLayout siblings that only take up space
        // while visible. That was the actual cause of the header (and the
        // filter pills above) visibly shifting down while loading/empty:
        // with nothing claiming the remaining height, the ColumnLayout's
        // total content height shrank to fit, and it isn't top-anchored
        // wide enough on its own to hold that space open across state
        // changes. Keeping one region permanently sized fixes it for good,
        // for ordinary browsing and search alike.
        Item {
            id: resultsArea
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 12
                visible: root.loading

                BusySpinner {
                    Layout.alignment: Qt.AlignHCenter
                    running: root.loading
                }

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: root.isSearchResults ? qsTr("Searching…") : qsTr("Loading…")
                    color: "#9E9E9E"
                    font.pixelSize: 13
                }
            }

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 18
                visible: !root.loading && !!root.errorMessage

                Label {
                    Layout.fillWidth: true
                    width: parent.width - 32
                    text: root.errorMessage
                    color: "#D32F2F"
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }

                Item {
                    id: reauthorizeButton
                    Layout.alignment: Qt.AlignHCenter
                    visible: !!root.service && root.service.shouldOfferReauthorize(root.errorMessage)
                    implicitWidth: reauthorizeLabel.implicitWidth + 40
                    implicitHeight: reauthorizeLabel.implicitHeight + 16

                    Rectangle {
                        anchors.fill: parent
                        radius: height / 2
                        color: reauthorizeMouseArea.pressed ? "#D0D0D0"
                                                            : (reauthorizeMouseArea.containsMouse ? "#E8E8E8" : "#F0F0F0")
                    }

                    Text {
                        id: reauthorizeLabel
                        anchors.centerIn: parent
                        text: qsTr("Reauthorize")
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        color: "#212121"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    MouseArea {
                        id: reauthorizeMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: signInDialog.open()
                    }
                }
            }

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 8
                visible: !root.loading && !!root.service && root.service.needsSignIn

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Sign in required")
                    color: "#9E9E9E"
                    font.pixelSize: 13
                }

                Button {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Sign In")
                    onClicked: {
                        signInDialog.open()
                    }
                }
            }

            ListView {
                id: listView
                anchors.fill: parent
                anchors.leftMargin: -5
                anchors.rightMargin: -5
                visible: !root.loading && !root.errorMessage && !(root.service && root.service.needsSignIn)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                spacing: 4
                model: root.items

                header: Item {
                    id: listHeaderContainer
                    width: listView.width
                    readonly property int folderArtSize: Math.round(Math.max(96, Math.min(listView.width * 0.66,
                                                                                           resultsArea.height * 0.42)))
                    readonly property int serviceRootHeight: root.isSmapiServiceRootPage ? 104 : 0
                    readonly property int folderHeaderHeight: root.hasFolderArt
                                                              ? folderArtSize + 10 + 26 + 10 + 18 + 14 + 32 + 16
                                                              : 0
                    property bool repinToBeginningAfterResize: false
                    height: serviceRootHeight + folderHeaderHeight
                    visible: height > 0

                    onFolderArtSizeChanged: {
                        repinToBeginningAfterResize = root.hasFolderArt && listView.atYBeginning
                    }

                    onHeightChanged: {
                        if (repinToBeginningAfterResize) {
                            repinToBeginningAfterResize = false
                            listView.positionViewAtBeginning()
                        }
                    }

                    ColumnLayout {
                        id: listHeader
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        visible: root.isSmapiServiceRootPage || root.hasFolderArt
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: listHeaderContainer.serviceRootHeight > 0 ? 86 : 0
                            visible: root.isSmapiServiceRootPage
                            spacing: 16

                            Item {
                                Layout.preferredWidth: 72
                                Layout.preferredHeight: 72
                                Layout.alignment: Qt.AlignVCenter

                                Rectangle {
                                    anchors.fill: parent
                                    radius: 14
                                    antialiasing: true
                                    layer.enabled: true
                                    layer.samples: 4
                                    color: "#BDBDBD"
                                    visible: serviceRootIconImage.status !== RoundedImage.Ready
                                }

                                RoundedImage {
                                    id: serviceRootIconImage
                                    anchors.fill: parent
                                    source: root.service ? root.service.iconSource : ""
                                    radius: 14
                                    visible: status === RoundedImage.Ready
                                }

                                Text {
                                    anchors.centerIn: parent
                                    visible: serviceRootIconImage.status !== RoundedImage.Ready
                                    text: "♪"
                                    font.pixelSize: 28
                                    color: "#7A7A7A"
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                text: root.service ? root.service.title : root.title
                                font.pixelSize: Math.round(23 * UiScale.factor)
                                font.weight: Typography.emphasisWeight
                                color: "#212121"
                                elide: Text.ElideRight
                            }
                        }

                        // Folder/album art header. This is a ListView
                        // header rather than a sibling above the ListView
                        // so the art, title, actions, and tracks all scroll
                        // as one page.
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.bottomMargin: 16
                            spacing: 10
                            visible: root.hasFolderArt

                            Item {
                                Layout.preferredWidth: listHeaderContainer.folderArtSize
                                Layout.preferredHeight: listHeaderContainer.folderArtSize
                                Layout.alignment: Qt.AlignHCenter

                                Rectangle {
                                    anchors.fill: parent
                                    radius: 8
                                    antialiasing: true
                                    layer.enabled: true
                                    layer.samples: 4
                                    color: "#BDBDBD"
                                    visible: folderArtImage.status !== RoundedImage.Ready
                                }

                                RoundedImage {
                                    id: folderArtImage
                                    anchors.fill: parent
                                    source: root.folderItem.imageUrl ? root.folderItem.imageUrl : ""
                                    radius: 8
                                    visible: status === RoundedImage.Ready
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                text: root.title
                                font.pixelSize: Math.round(20 * UiScale.factor)
                                font.weight: Typography.emphasisWeight
                                color: "#212121"
                                elide: Text.ElideRight
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: text.length > 0
                                text: root.folderItem.artist ? root.folderItem.artist : ""
                                font.pixelSize: 14
                                color: "#212121"
                                opacity: 0.65
                                elide: Text.ElideRight
                                horizontalAlignment: Text.AlignHCenter
                            }

                            RowLayout {
                                id: playPillRow
                                Layout.fillWidth: true
                                Layout.topMargin: 4
                                spacing: 8

                                Rectangle {
                                    id: playPill
                                    readonly property int maxWidth: root.width - shuffleButton.implicitWidth
                                                                     - folderMenuButton.implicitWidth - playPillRow.spacing * 2
                                    Layout.preferredWidth: Math.min(24 + 10 + 4 + 16 + 4 + roomNameMeasure.implicitWidth,
                                                                    maxWidth)
                                    Layout.preferredHeight: 32
                                    radius: height / 2
                                    color: playPillMouseArea.containsMouse ? "#E8E8E8" : "#F0F0F0"

                                    Label {
                                        id: roomNameMeasure
                                        visible: false
                                        text: roomNameLabel.text
                                        font: roomNameLabel.font
                                    }

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 4
                                        anchors.rightMargin: 16
                                        spacing: 10

                                        Item {
                                            Layout.preferredWidth: 24
                                            Layout.preferredHeight: 24

                                            Image {
                                                anchors.centerIn: parent
                                                source: "../resources/icons/play.svg"
                                                sourceSize.width: 16
                                                sourceSize.height: 16
                                            }
                                        }

                                        Label {
                                            id: roomNameLabel
                                            Layout.fillWidth: true
                                            text: root.zone ? root.zone.roomName : qsTr("No zone selected")
                                            font.pixelSize: 14
                                            font.weight: Typography.emphasisWeight
                                            color: "#212121"
                                            elide: Text.ElideRight
                                        }
                                    }

                                    MouseArea {
                                        id: playPillMouseArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        enabled: !!root.zone && !!root.folderItem.uri
                                        onClicked: root.zone.playItem(root.folderItem)
                                    }
                                }

                                Item {
                                    Layout.fillWidth: true
                                }

                                IconButton {
                                    id: shuffleButton
                                    iconSource: "../resources/icons/shuffle.svg"
                                    iconSize: 18
                                    idleColor: "#F0F0F0"
                                    hoverColor: "#E4E4E4"
                                    pressedColor: "#D0D0D0"
                                }

                                IconButton {
                                    id: folderMenuButton
                                    visible: !!root.folderItem.uri
                                    iconSource: "../resources/icons/three_dots_vertical.svg"
                                    iconSize: 16
                                    idleColor: "#F0F0F0"
                                    hoverColor: "#E4E4E4"
                                    pressedColor: "#D0D0D0"
                                    onClicked: {
                                        rowMenu.currentItem = root.folderItem
                                        rowMenu.parent = folderMenuButton
                                        rowMenu.x = folderMenuButton.width - rowMenu.width
                                        rowMenu.y = folderMenuButton.height + 4
                                        rowMenu.open()
                                    }
                                }
                            }
                        }
                    }
                }

                delegate: MusicServiceRow {
                    id: rowItem
                    width: listView.width - 16
                    title: modelData.title
                    imageUrl: modelData.imageUrl
                    showChevron: !!modelData.container
                    showMenu: !!modelData.uri
                    showPlayOverlay: !modelData.container && !!modelData.uri
                    menuOpen: rowMenu.visible && rowMenu.parent === rowItem
                    // Only within an actual album/playlist listing, and
                    // only for the playable tracks in it -- a list position
                    // isn't a meaningful "track number" for ordinary
                    // browsing/search results.
                    trackNumber: (root.hasFolderArt && !modelData.container)
                                 ? String(index + 1).padStart(2, "0") : ""
                    circularIcon: false

                    onClicked: {
                        root.recordFavouriteUse(modelData)
                        if (modelData.container) {
                            root.stack.pushFolder(root.pageComponent, {
                                title: modelData.title,
                                service: root.service,
                                // Same shape as BB10: QML passes the clicked
                                // item back to the current service. C++ owns
                                // the SMAPI-vs-ContentDirectory decision.
                                objectId: modelData.browseId || modelData.id,
                                browseItem: modelData,
                                stack: root.stack,
                                pageComponent: root.pageComponent,
                                folderItem: modelData
                            })
                        } else if (root.zone && modelData.uri) {
                            // Playable leaf item -- modelData already carries
                            // everything ZonePlayer::playItem() needs
                            // (uri/upnpClass/didlId/parentId/desc), computed
                            // by the service itself at parse time (see
                            // SonosLibraryService/SmapiService.cpp).
                            root.zone.playItem(modelData)
                        }
                    }

                    onMenuRequested: {
                        rowMenu.currentItem = modelData
                        rowMenu.parent = rowItem
                        rowMenu.x = rowItem.width - rowMenu.width - 8
                        rowMenu.y = rowItem.height
                        rowMenu.open()
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: listView.count === 0
                    text: qsTr("Empty")
                    color: "#9E9E9E"
                    font.pixelSize: 13
                }
            }
        }
    }
}
