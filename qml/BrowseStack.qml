import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The "Browse" panel's navigation stack. Base page is the flat list of
// installed music services; tapping one -- or, from within a
// BrowseListPage, tapping a folder -- pushes a new page that slides in
// from the right to sit directly over the current one, mirroring how
// Sonos' own controllers navigate SMAPI content trees. A search-results
// page (see BrowseListPage.qml's search field) instead slides up from the
// bottom, covering whatever was showing rather than "descending a level"
// -- and slides back down on its way out -- since a search reads as
// opening a panel over the current view, not going deeper into it. Only
// the StackView itself clips (so a page sliding in/out doesn't spill
// outside the Browse panel's rounded edge) -- nothing inside an
// individual page adds its own clipping beyond that, so album art
// thumbnails are never cut off by the transition.
StackView {
    id: stack

    initialItem: homePageComponent
    clip: true

    // The zone play() targets -- bound once here rather than threaded
    // through every pushFolder()/pushSearchResults() properties dict, since
    // it's the same zone for the whole stack and BrowseListPage's own
    // recursive pushes reuse the same pageComponent (see its "zone: stack.zone"
    // binding below), which already reacts if the user switches zones
    // mid-browse.
    property var zone

    readonly property bool canGoBack: depth > 1

    // Ordinary "descend into a folder" navigation.
    function pushFolder(component, properties) {
        pushEnter = folderPushEnter
        pushExit = folderPushExit
        push(component, properties)
    }

    // A search-results page -- see the class comment above.
    function pushSearchResults(component, properties) {
        pushEnter = searchPushEnter
        pushExit = searchPushExit
        push(component, properties)
    }

    function goBack() {
        if (!canGoBack)
            return

        if (currentItem && currentItem.isSearchResults) {
            popEnter = searchPopEnter
            popExit = searchPopExit
        } else {
            popEnter = folderPopEnter
            popExit = folderPopExit
        }
        pop()
    }

    // Horizontal slide -- StackView's own built-in default push transition,
    // spelled out explicitly rather than left implicit, since pushEnter/
    // pushExit/popEnter/popExit now get reassigned per-navigation-kind
    // above and need a real value to switch back to.
    Transition {
        id: folderPushEnter
        NumberAnimation { property: "x"; from: stack.width; to: 0; duration: 220; easing.type: Easing.OutCubic }
    }
    Transition {
        id: folderPushExit
        NumberAnimation { property: "x"; from: 0; to: -stack.width * 0.3; duration: 220; easing.type: Easing.OutCubic }
    }
    Transition {
        id: folderPopEnter
        NumberAnimation { property: "x"; from: -stack.width * 0.3; to: 0; duration: 220; easing.type: Easing.OutCubic }
    }
    Transition {
        id: folderPopExit
        NumberAnimation { property: "x"; from: 0; to: stack.width; duration: 220; easing.type: Easing.OutCubic }
    }

    // Vertical slide for a search-results page. The item underneath stays
    // completely still in both directions -- it's not "moving aside", the
    // results panel is sliding over/off of it -- so only the *incoming*
    // (push) or *outgoing* (pop) item itself actually animates.
    Transition {
        id: searchPushEnter
        NumberAnimation { property: "y"; from: stack.height; to: 0; duration: 240; easing.type: Easing.OutCubic }
    }
    Transition {
        id: searchPushExit
        // No-op: the covered page underneath doesn't move.
        PauseAnimation { duration: 240 }
    }
    Transition {
        id: searchPopEnter
        // No-op: the revealed page underneath doesn't move.
        PauseAnimation { duration: 240 }
    }
    Transition {
        id: searchPopExit
        NumberAnimation { property: "y"; from: 0; to: stack.height; duration: 240; easing.type: Easing.OutCubic }
    }

    Component {
        id: browsePageComponent

        BrowseListPage {
            stack: stack
            pageComponent: browsePageComponent
            zone: stack.zone
        }
    }

    Component {
        id: recentlyPlayedPageComponent

        RecentlyPlayedPage {
            stack: stack
        }
    }

    Component {
        id: allServicesPageComponent

        AllServicesPage {
            stack: stack
            pageComponent: browsePageComponent
        }
    }

    Component {
        id: homePageComponent

        BrowseHome {
            browseStack: stack
            pageComponent: browsePageComponent
            recentlyPlayedComponent: recentlyPlayedPageComponent
            allServicesComponent: allServicesPageComponent
        }
    }
}
