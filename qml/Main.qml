import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Shapes

ApplicationWindow {
    id: window

    width: 1100
    height: 700
    // Starts hidden: on Windows, main.cpp installs the WM_NCCALCSIZE chrome
    // filter and then shows the window itself, so the native title bar
    // never flashes on screen for a frame before collapsing.
    visible: false
    title: qsTr("Room Tunes")

    // Windows keeps its native frame (DWM draws the rounded corners/shadow/
    // border; WindowsChrome.cpp collapses the title bar visually via
    // WM_NCCALCSIZE). GNOME/KDE have no equivalent, so they go fully
    // frameless and RoomTunes draws its own chrome instead.
    flags: PlatformChrome.isWindows ? Qt.Window : (Qt.Window | Qt.FramelessWindowHint)
    color: PlatformChrome.isWindows ? "#EAEAEA" : "transparent"

    readonly property bool maximizedOrFullScreen:
        visibility === Window.Maximized || visibility === Window.FullScreen

    // KDE's compositor (KWin) doesn't shadow client-side-decorated windows
    // the way GNOME's Mutter does, so only KDE needs an inset transparent
    // margin to self-paint a shadow into (linux-chrome.cpp/shadow-chrome.cpp
    // in qexed). That margin doubles as extra resize-grab area.
    readonly property int shadowMargin: (PlatformChrome.isKde && !maximizedOrFullScreen) ? 10 : 0
    readonly property int contentRadius: (!PlatformChrome.isWindows && !maximizedOrFullScreen) ? 12 : 0

    // NowPlayingWide's own cap in a very wide window -- past this, its
    // title/artist column would just be mostly empty space next to a
    // short track title, so growth stops here instead (referenced by
    // both nowPlayingColumn's own Layout.maximumWidth below and
    // maxContentWidth's sum).
    readonly property int nowPlayingMaxWidth: 640

    // Total width of the 3-column group once every column has hit its
    // own individual cap (zonesColumn's is implicitly 280, since
    // UiScale.factor never exceeds 1.0) -- the whole RowLayout's own
    // Layout.maximumWidth below, so a very wide window centers the
    // group with grey margins on both sides instead of continuing to
    // stretch it.
    readonly property int maxContentWidth: 280 + nowPlayingMaxWidth + BrowseGrid.maxWidth
                                            + 20 * 2 // RowLayout spacing between the 3 columns

    // Hard floor for the OS-level window resize itself, tight enough to
    // preserve real shrink range but still enough to stop the
    // RowLayout's content from genuinely overflowing the window: without
    // this, dragging narrower than the sum of every visible column's own
    // Layout.minimumWidth just pushes Browse off the window's right edge
    // instead of anything actually resizing further.
    //
    // Only nowPlaying's and browseColumn's own floors -- not
    // zonesColumn's -- since zonesColumn hides itself (see its own
    // visible binding below) once it would otherwise need to shrink to
    // that floor, so the true achievable minimum never actually needs
    // zonesColumn's floor accounted for.
    minimumWidth: nowPlaying.minimumCompactWidth + BrowseGrid.minimumColumnWidth
                  + 20 // RowLayout spacing -- one gap, between 2 columns, once Zones is hidden
                  + 20 * 2 // RowLayout Layout.margins (left + right)
                  + shadowMargin * 2

    property var selectedZone: null
    property bool compactZonesExpanded: false

    // Drives UiScale.factor (Zones column width + headline text) and
    // BrowseGrid.factor (Browse column width + tile grid column count)
    // as the window narrows -- see UiScale.qml for why NowPlaying and
    // everything else is excluded.
    //
    // TEMPORARILY a direct Binding instead of a debounced Timer, to A/B
    // whether debouncing is still worth it now that both UiScale and
    // BrowseGrid hang off the same signal -- a live drag-resize fires
    // window.width changes dozens of times a second, and each one
    // re-triggers font relayout across every headline Text *and* a
    // GridLayout reflow across every visible BrowseTile, not just
    // zonesColumn's own RowLayout pass. If this turns out to feel
    // sluggish again, reintroduce a Timer here: restart it from an
    // onWidthChanged handler and only write windowWidth to both
    // singletons once it fires, instead of writing directly below.
    Binding {
        target: UiScale
        property: "windowWidth"
        value: window.width
    }

    Binding {
        target: BrowseGrid
        property: "windowWidth"
        value: window.width
    }

    // A network change (see Household::onNetworkChanged) destroys every
    // known ZonePlayer, including whichever one is currently selected --
    // dropped here *before* that happens (aboutToResetZones fires ahead of
    // the actual teardown) rather than left to become a stale reference.
    Connections {
        target: household
        function onAboutToResetZones() {
            window.selectedZone = null
        }
    }

    // Drives the window-wide dim behind an open ActionMenu (see
    // overlayDim below) -- true for as long as either settings menu is
    // visible, including during its own fade-out.
    readonly property bool anyActionMenuOpen:
        zonesPanel.settingsMenuOpen || zoneCompact.settingsMenuOpen || browseSettingsMenu.visible

    onSelectedZoneChanged: {
        if (selectedZone) {
            selectedZone.refreshTransportState()
            selectedZone.refreshVolume()
        }
        queueModel.zone = selectedZone
    }

    Item {
        anchors.fill: parent

        // Sits above zonesColumn (and everything else) so the zone
        // group-drag ghost (see sharedDragGhost below) isn't clipped by
        // ZonesPanel's own card list the moment it moves outside
        // whichever card started the drag.
        Item {
            id: dragOverlayLayer
            anchors.fill: parent
            z: 9999

            // Shared by every ZoneGroupCard (via ZonesPanel/ZoneGroupList)
            // -- see ZoneDragGhost.qml's own comment for why this is one
            // statically-declared item passed down to every card, rather
            // than each card owning (and dynamically reparenting into
            // this layer) its own.
            ZoneDragGhost {
                id: sharedDragGhost
            }
        }

        Rectangle {
            id: background
            anchors.fill: parent
            anchors.margins: window.shadowMargin
            radius: window.contentRadius
            color: "#EAEAEA"
            clip: true

            layer.enabled: PlatformChrome.isKde && !window.maximizedOrFullScreen
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: "#40000000"
                shadowBlur: 0.7
                shadowVerticalOffset: 3
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                TitleBar {
                    id: titleBar
                    Layout.fillWidth: true
                    appWindow: window
                    // The "Zones" column isn't as wide as "Browse", so the
                    // Now Playing column (nowPlayingColumn) isn't centered
                    // in the window either -- center the title over it
                    // specifically rather than over the whole title bar.
                    centerX: nowPlayingColumn.mapToItem(titleBar, nowPlayingColumn.width / 2, 0).x
                }

                RowLayout {
                    // fillWidth still grows normally up to maxContentWidth,
                    // then AlignHCenter takes over -- once every column
                    // below is at its own individual cap, this row's own
                    // resolved width sits below the ColumnLayout's real
                    // width, and alignment centers it there instead of
                    // stretching further. Safe against the same
                    // implicit-width-vs-real-width mixup NowPlayingCompact's
                    // art centering hit (see its own comment) -- this
                    // ColumnLayout's real width comes from anchors.fill on
                    // the window itself, not from any child's implicit
                    // size, so there's no wrong "implicit" width for
                    // AlignHCenter to center against by mistake here.
                    Layout.fillWidth: true
                    Layout.maximumWidth: window.maxContentWidth
                    Layout.alignment: Qt.AlignHCenter
                    Layout.fillHeight: true
                    Layout.margins: 20
                    spacing: 20

                    // Plain Item (not a Layout type) so Layout.preferredWidth actually
                    // sticks -- a ColumnLayout used directly as the RowLayout-managed,
                    // width-constrained item doesn't reliably hold that width.
                    Item {
                        id: zonesColumn
                        Layout.preferredWidth: Math.round(280 * UiScale.factor)
                        Layout.fillHeight: true
                        clip: true
                        // Below the width where this would otherwise be
                        // stuck at its own cramped floor (UiScale.minFactor)
                        // indefinitely, hide it outright instead and let
                        // Now Playing/Browse have that space -- an
                        // invisible RowLayout child is skipped entirely
                        // (no reserved gap), same as BrowseTile's own
                        // index<5 visibility trick. TEMPORARY: trying this
                        // instead of moving Zones somewhere else (e.g.
                        // folded into the Browse panel) to see how it
                        // feels before committing to either.
                        visible: UiScale.factor > UiScale.minFactor
                        onVisibleChanged: {
                            if (visible)
                                window.compactZonesExpanded = false
                        }

                        ZonesPanel {
                            id: zonesPanel
                            anchors.fill: parent
                            dragGhost: sharedDragGhost
                            selectedZone: window.selectedZone
                            onZoneSelected: (zone) => window.selectedZone = zone
                        }
                    }

                    // Same Item-not-Layout pattern as zonesColumn above.
                    Item {
                        id: nowPlayingColumn
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        // Protects NowPlayingCompact's transport controls
                        // from shrinking past nowPlaying.compactSizeFloor
                        // -- see NowPlayingPanel.qml's minimumCompactWidth.
                        // zonesColumn absorbs the squeeze instead once
                        // this floor is reached.
                        Layout.minimumWidth: nowPlaying.minimumCompactWidth
                        // See window.nowPlayingMaxWidth's own comment.
                        Layout.maximumWidth: window.nowPlayingMaxWidth

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 20

                            // Sized to its own content, not fillHeight -- a ColumnLayout
                            // much taller than its content, with no child marked
                            // fillHeight, still spreads the surplus across children and
                            // centers each one in its (inflated) cell rather than
                            // packing at the top. "Up Next" is the one that should
                            // reasonably grow (it'll hold a variable-length list).
                            Panel {
                                Layout.fillWidth: true
                                // NowPlayingPanel.qml's own preferredHeight,
                                // not a flat constant -- its narrow-panel
                                // layout (NowPlayingCompact.qml) needs more
                                // vertical room than the normal 280px to
                                // show its transport controls without
                                // clipping them, unlike the side-by-side
                                // NowPlayingWide.qml arrangement that 280px
                                // was originally sized around.
                                Layout.preferredHeight: nowPlaying.preferredHeight
                                color: nowPlaying.panelBackgroundColor

                                NowPlayingPanel {
                                    id: nowPlaying
                                    anchors.fill: parent
                                    zone: window.selectedZone
                                }
                            }

                            // Stands in for zonesColumn's full list once
                            // that's hidden (see its own visible binding)
                            // -- just enough to see/change which zone
                            // Now Playing and Queue below refer to,
                            // without needing the room back that a full
                            // list display would take.
                            ZoneGroupCompact {
                                id: zoneCompact
                                Layout.fillWidth: true
                                Layout.fillHeight: window.compactZonesExpanded
                                Layout.preferredHeight: window.compactZonesExpanded
                                    ? Math.max(180, nowPlayingColumn.height
                                                - nowPlaying.preferredHeight
                                                - compactQueuePanel.Layout.preferredHeight
                                                - 40)
                                    : implicitHeight
                                visible: !zonesColumn.visible
                                expanded: window.compactZonesExpanded
                                dragGhost: sharedDragGhost
                                selectedZone: window.selectedZone
                                onZoneSelected: (zone) => {
                                    window.selectedZone = zone
                                    window.compactZonesExpanded = false
                                }
                                onExpandRequested: window.compactZonesExpanded = !window.compactZonesExpanded

                                Behavior on Layout.preferredHeight {
                                    NumberAnimation { duration: 260; easing.type: Easing.OutCubic }
                                }
                            }

                            Panel {
                                id: queuePanel
                                Layout.fillWidth: true
                                Layout.fillHeight: !window.compactZonesExpanded
                                Layout.minimumHeight: 140
                                visible: !window.compactZonesExpanded

                                QueuePanel {
                                    anchors.fill: parent
                                    queue: queueModel
                                }
                            }

                            Panel {
                                id: compactQueuePanel
                                Layout.fillWidth: true
                                Layout.preferredHeight: 44
                                visible: window.compactZonesExpanded
                                radius: height / 2

                                QueueCompact {
                                    anchors.fill: parent
                                    onExpandRequested: window.compactZonesExpanded = false
                                }
                            }
                        }
                    }

                    // Same Item-not-Layout pattern as zonesColumn above.
                    // Unlike zonesColumn (which only ever shrinks from its
                    // 280 baseline), this can also grow past its own 320
                    // baseline in a wide window -- see BrowseGrid.qml for
                    // why a tile grid benefits from that where a room list
                    // doesn't.
                    Item {
                        id: browseColumn
                        Layout.preferredWidth: BrowseGrid.idealWidth
                        Layout.minimumWidth: BrowseGrid.minimumColumnWidth
                        Layout.fillHeight: true

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 12

                            Panel {
                                Layout.fillWidth: true
                                Layout.fillHeight: true

                                // Each page owns its own header (title +
                                // back button + search icon, see
                                // BrowseStack.qml/BrowseListPage.qml) so the
                                // whole header slides in/out with the page
                                // beneath it, rather than staying fixed
                                // above a separately-animated list.
                                BrowseStack {
                                    id: browseStack
                                    anchors.fill: parent
                                    anchors.margins: 20
                                    zone: window.selectedZone
                                }
                            }

                            // Reclaims, below the panel, exactly the height
                            // the Zones panel's own header row (pause/
                            // settings icons) takes up above its list --
                            // same icon row height, same spacing before/
                            // after -- so the settings icon here sits
                            // "under" the Browse panel rather than
                            // overlapping its content.
                            Item {
                                Layout.fillWidth: true
                                Layout.preferredHeight: zonesPanel.headerHeight

                                IconButton {
                                    id: browseSettingsButton
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    iconSource: "../resources/icons/settings.svg"
                                    onPressed: browseSettingsMenu.open()
                                }

                                // Opens upward (not downward, like the
                                // Zones one) -- this button sits right at
                                // the bottom of the window, so a
                                // downward-opening menu would run off the
                                // bottom edge. Still right-aligned to the
                                // button for the same clipping reason.
                                ActionMenu {
                                    id: browseSettingsMenu
                                    parent: browseSettingsButton
                                    x: browseSettingsButton.width - width
                                    y: -height - 6
                                    items: [qsTr("Add new service")]
                                }
                            }
                        }
                    }
                }
            }
        }

        // Window-wide dim behind an open ActionMenu -- ActionMenu itself
        // suppresses its own default modal-dimming visual (Overlay.modal:
        // Item {}) so this is the only dimming that actually renders,
        // fading in/out in step with the menu's own opacity transition
        // rather than snapping instantly the way QQC2's default modal
        // overlay would. Popups always render into Qt Quick Controls'
        // Overlay layer, which sits above ordinary Item content
        // regardless of declaration order, so this doesn't need to be
        // reordered relative to the menus themselves to stay underneath.
        Rectangle {
            id: overlayDim
            anchors.fill: parent
            color: "#59000000"
            opacity: window.anyActionMenuOpen ? 1 : 0
            visible: opacity > 0

            Behavior on opacity {
                NumberAnimation { duration: 180; easing.type: Easing.OutQuad }
            }
        }

        ResizeBorder {
            appWindow: window
            // No point grabbing for a resize on a maximized/full-screen window.
            enabled: !window.maximizedOrFullScreen
            visible: enabled
        }
    }
}
