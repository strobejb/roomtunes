import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The Browse panel's landing page -- pushed as BrowseStack.qml's
// initialItem instead of a plain vertical list of installed services.
// Three horizontally-wrapping icon-tile sections (3-6+ per row --
// BrowseGrid.columnsFor(), a step function of the Browse column's own
// width, see BrowseGrid.qml and Main.qml's browseColumn), each capped
// to its first 5 items (or, once this page itself gets too short for
// two rows per section to fit comfortably, just its first row -- see
// compactSections) with a header chevron pushing the full list: Recently
// Played (this
// app's own client-side history -- see RecentlyPlayedModel.h for why,
// Sonos itself doesn't expose one), Sonos Favourites, then installed
// music services. Services and Favourites both feed into the same
// browsing stack as entry points -- a service tile pushes that service's
// own root folder, a favourite tile pushes a browse of the favourite
// itself (through the library service FV:2 was browsed through) --
// regardless of whether it turns out to be a container or a single track/
// station, unlike BrowseListPage.qml's own row click, which only pushes
// for an actual container and plays a leaf directly. Recently Played is
// the one exception: its tiles play immediately, since a play-history
// entry is always a single track/station with nothing to browse into
// (see its own onClicked comment below for why).
Item {
    id: root

    // Named "browseStack", not "stack" -- BrowseHome is instantiated via
    // BrowseStack.qml's "initialItem: homePageComponent" rather than via
    // stack.push(component, properties), so unlike every *pushed* page
    // (BrowseListPage.qml, RecentlyPlayedPage.qml, AllServicesPage.qml),
    // there's no push()-supplied initialProperties object to correctly
    // resolve "stack: stack" afterward and paper over the same self-
    // shadowing collision those pages' own declarative bindings have too
    // (see BrowseStack.qml's Component blocks) -- confirmed via a debug
    // build: this ended up genuinely null, not just cosmetically flagged,
    // throwing "Cannot call method 'pushFolder' of null" the first time a
    // click actually used it.
    property StackView browseStack
    property Component pageComponent
    // Named distinctly from BrowseStack.qml's own "recentlyPlayedPageComponent"/
    // "allServicesPageComponent" Component ids that get passed in as these
    // values -- an identically-named property/id pair (Type { name: name })
    // is a real QML footgun: the unqualified name on the right resolves to
    // this object's *own* same-named property first (per QML's documented
    // scope order: object's own properties before other ids in the
    // document), turning it into a self-referencing binding loop instead
    // of picking up the outer id. Confirmed via a debug build -- this
    // exact collision on these two properties (plus, seemingly as a
    // consequence, "stack" alongside them) logged three "Binding loop
    // detected" warnings and preceded a crash a short time later.
    property Component recentlyPlayedComponent
    property Component allServicesComponent

    // Re-read whenever the household's service set changes (fresh
    // install, network reconnect, ...) -- household.libraryService() is a
    // plain Q_INVOKABLE, not a bindable property, so it needs an explicit
    // refresh trigger rather than updating on its own.
    property var libraryService: household.libraryService()
    property var favouriteItems: []
    property bool favouritesLoading: true
    property string favouritesError: ""

    Connections {
        target: household
        function onMusicServicesChanged() {
            root.libraryService = household.libraryService()
        }
    }

    // Same stale-reply guard as BrowseListPage.qml's own requestToken.
    property string favouritesToken: ""

    function loadFavourites() {
        if (!root.libraryService)
            return
        root.favouritesLoading = true
        root.favouritesError = ""
        root.favouritesToken = "favourites|" + Math.random()
        // Sonos Favorites' own well-known ContentDirectory container id --
        // an ordinary Browse target, not a SonosLibraryService root
        // category (see its own class comment), so it's reachable through
        // any zone-backed service that just forwards objectId straight to
        // ZonePlayer::browse(), which the library service does for
        // anything other than "root".
        root.libraryService.browse(root.favouritesToken, "FV:2")
    }

    onLibraryServiceChanged: {
        if (root.libraryService && root.favouritesLoading)
            loadFavourites()
    }

    Component.onCompleted: loadFavourites()

    Connections {
        target: root.libraryService

        function onBrowseFinished(token, ok, message, results) {
            if (token !== root.favouritesToken)
                return
            root.favouritesLoading = false
            if (ok) {
                root.favouriteItems = results.slice(0, 5)
            } else {
                root.favouritesError = message
                root.favouriteItems = []
            }
        }
    }

    // A favourite tile's own click behaviour -- always pushes a browse
    // page for the favourite itself (through the library service, which
    // is what FV:2 was browsed through in the first place), the same way
    // a service tile pushes that service's own root -- not conditional on
    // modelData.container the way BrowseListPage.qml's ordinary row click
    // is, since favourites are meant as browsable entry points here
    // (mirroring the "services" section's own tiles) rather than
    // one-tap-to-play shortcuts. A favourite that turns out to have
    // nothing further to browse into (a single saved track/station, not a
    // playlist/album) still pushes the page -- it just lands on
    // BrowseListPage.qml's own empty/error state, same as browsing any
    // other folder that comes back empty.
    function openFavourite(item) {
        root.browseStack.pushFolder(root.pageComponent, {
            title: item.title,
            service: root.libraryService,
            objectId: item.id,
            stack: root.browseStack,
            pageComponent: root.pageComponent,
            folderItem: item
        })
    }

    // Below this, three sections each wrapping their up-to-5 items into
    // 2 rows would need more vertical room than a short window actually
    // has, forcing the Flickable below into constant scrolling just to
    // see "Your Services" at all -- capped to a single row per section
    // instead once height gets tight. root.height (not e.g. Main.qml's
    // browseColumn) since this Item already sits fully sized within
    // BrowseStack's own content area (StackView items fill available
    // space, same as width), no threading-through needed.
    readonly property bool compactSections: root.height < 550

    ColumnLayout {
        anchors.fill: parent
        spacing: 20

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: qsTr("Browse")
                font.pixelSize: Math.round(18 * UiScale.factor)
                font.bold: true
                color: "#212121"
            }

            SearchIconButton {
            }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: sectionsColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: sectionsColumn
                width: parent.width
                spacing: 24

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    visible: recentlyPlayedRepeater.count > 0

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Recently Played")
                            font.pixelSize: 14
                            font.bold: true
                            color: "#212121"
                        }

                        IconButton {
                            iconSource: "../resources/icons/chevron_right.svg"
                            iconSize: 16
                            onClicked: root.browseStack.pushFolder(root.recentlyPlayedComponent, {
                                stack: root.browseStack
                            })
                        }
                    }

                    GridLayout {
                        id: recentlyPlayedGrid
                        // Tiles exactly fill this row's available width,
                        // not each tile's own (fixed) implicitWidth --
                        // otherwise columns * BrowseTile.implicitWidth
                        // plus gaps can exceed the panel's actual content
                        // width and overflow past its edge instead of
                        // being evenly distributed within it.
                        //
                        // Both derived from sectionsColumn.width (a
                        // plain, top-down "width: parent.width" binding
                        // with no dependency on any child's size), not
                        // this GridLayout's *own* width -- binding a
                        // tile's Layout.preferredWidth back to a width
                        // that this same GridLayout's own layout pass
                        // helps determine is a real circular dependency:
                        // Qt Quick Layouts re-polishes on every geometry
                        // change, so tileWidth changing the tiles' sizes
                        // changes this GridLayout's content, which can
                        // change its resolved width again, forever.
                        // Confirmed via a live hang: CPU time climbing
                        // continuously with "Not Responding" and nothing
                        // rendering, immediately after this exact pattern
                        // was introduced.
                        //
                        // columnCount, not "columns" -- GridLayout already
                        // has a built-in columns property (the one actually
                        // assigned below), so a same-named custom property
                        // here would collide with it exactly like Item's
                        // built-in scale did in NowPlayingTransportControls.qml.
                        readonly property int columnCount: BrowseGrid.columnsFor(sectionsColumn.width)
                        readonly property real tileWidth: (sectionsColumn.width - (columnCount - 1) * columnSpacing) / columnCount
                        Layout.fillWidth: true
                        columns: columnCount
                        columnSpacing: 8
                        rowSpacing: 8

                        Repeater {
                            id: recentlyPlayedRepeater
                            model: recentlyPlayedModel

                            BrowseTile {
                                visible: index < (root.compactSections ? recentlyPlayedGrid.columnCount : 5)
                                Layout.preferredWidth: recentlyPlayedGrid.tileWidth
                                title: model.title
                                imageUrl: model.imageUrl
                                circularIcon: false
                                // Plays directly rather than pushing a
                                // browse page (unlike favourites/services
                                // below) -- a played-history entry is
                                // always a single leaf track/station, with
                                // no browsable container of its own to
                                // push into, and no record of which
                                // service it originally came from to
                                // browse through even if it did (see
                                // RecentlyPlayedModel.h -- entries only
                                // keep enough to replay an item, not to
                                // browse from it).
                                onClicked: {
                                    if (root.browseStack.zone)
                                        root.browseStack.zone.playItem(model.item)
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    visible: root.favouritesLoading || root.favouriteItems.length > 0 || root.favouritesError.length > 0

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Favourites")
                            font.pixelSize: 14
                            font.bold: true
                            color: "#212121"
                        }

                        IconButton {
                            iconSource: "../resources/icons/chevron_right.svg"
                            iconSize: 16
                            onClicked: root.browseStack.pushFolder(root.pageComponent, {
                                title: qsTr("Favourites"),
                                service: root.libraryService,
                                objectId: "FV:2",
                                stack: root.browseStack,
                                pageComponent: root.pageComponent
                            })
                        }
                    }

                    BusyIndicator {
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        visible: root.favouritesLoading
                        running: root.favouritesLoading
                    }

                    Label {
                        visible: !root.favouritesLoading && root.favouritesError.length > 0
                        text: root.favouritesError
                        color: "#D32F2F"
                        font.pixelSize: 12
                    }

                    GridLayout {
                        id: favouritesGrid
                        // sectionsColumn.width, not this GridLayout's own
                        // width -- see recentlyPlayedGrid.tileWidth's
                        // comment for why (a genuine circular layout
                        // dependency, confirmed via a live hang).
                        //
                        // columnCount, not "columns" -- see
                        // recentlyPlayedGrid's own comment for why.
                        readonly property int columnCount: BrowseGrid.columnsFor(sectionsColumn.width)
                        readonly property real tileWidth: (sectionsColumn.width - (columnCount - 1) * columnSpacing) / columnCount
                        Layout.fillWidth: true
                        visible: !root.favouritesLoading && root.favouriteItems.length > 0
                        columns: columnCount
                        columnSpacing: 8
                        rowSpacing: 8

                        Repeater {
                            model: root.favouriteItems

                            BrowseTile {
                                visible: index < (root.compactSections ? favouritesGrid.columnCount : 5)
                                Layout.preferredWidth: favouritesGrid.tileWidth
                                title: modelData.title
                                imageUrl: modelData.imageUrl
                                circularIcon: false
                                onClicked: root.openFavourite(modelData)
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Your Services")
                            font.pixelSize: 14
                            font.bold: true
                            color: "#212121"
                        }

                        IconButton {
                            iconSource: "../resources/icons/chevron_right.svg"
                            iconSize: 16
                            onClicked: root.browseStack.pushFolder(root.allServicesComponent, {
                                stack: root.browseStack,
                                pageComponent: root.pageComponent
                            })
                        }
                    }

                    Label {
                        // servicesRepeater.count, not musicServiceModel.rowCount()
                        // -- a plain method call's result isn't tracked by QML's
                        // binding engine, so it never re-evaluates when the
                        // model actually repopulates after this binding first
                        // runs (confirmed via a debug build: this stayed stuck
                        // visible even once the services below had loaded).
                        // Repeater.count is a real NOTIFYing property that
                        // updates as its model does.
                        visible: servicesRepeater.count === 0
                        text: qsTr("No music services found")
                        color: "#9E9E9E"
                        font.pixelSize: 13
                    }

                    GridLayout {
                        id: servicesGrid
                        // sectionsColumn.width, not this GridLayout's own
                        // width -- see recentlyPlayedGrid.tileWidth's
                        // comment for why (a genuine circular layout
                        // dependency, confirmed via a live hang).
                        //
                        // columnCount, not "columns" -- see
                        // recentlyPlayedGrid's own comment for why.
                        readonly property int columnCount: BrowseGrid.columnsFor(sectionsColumn.width)
                        readonly property real tileWidth: (sectionsColumn.width - (columnCount - 1) * columnSpacing) / columnCount
                        Layout.fillWidth: true
                        columns: columnCount
                        columnSpacing: 8
                        rowSpacing: 8

                        Repeater {
                            id: servicesRepeater
                            model: musicServiceModel

                            BrowseTile {
                                // Only the first 5 (or, once
                                // compactSections kicks in, only the
                                // first row) -- an invisible GridLayout
                                // item skips its cell entirely rather
                                // than reserving blank space, so this
                                // doesn't leave a gap.
                                visible: index < (root.compactSections ? servicesGrid.columnCount : 5)
                                Layout.preferredWidth: servicesGrid.tileWidth
                                title: model.title
                                imageUrl: model.imageUrl

                                onClicked: root.browseStack.pushFolder(root.pageComponent, {
                                    title: model.title,
                                    service: model.serviceObject,
                                    objectId: "root",
                                    stack: root.browseStack,
                                    pageComponent: root.pageComponent
                                })
                            }
                        }
                    }
                }
            }
        }
    }
}
