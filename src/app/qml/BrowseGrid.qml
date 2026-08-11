pragma Singleton
import QtQuick

// Shared sizing policy for the Browse column's icon-tile grids
// (BrowseHome.qml's Recently Played/Favourites/Your Services sections).
// The column's own outer width (see idealWidth below, read by Main.qml's
// browseColumn) tracks window.width continuously/proportionally, exactly
// like zonesColumn's UiScale.factor -- but unlike Zones (which only ever
// shrinks from its 280 baseline), Browse can also grow past its baseline
// in a wide window, since more room genuinely helps a tile grid.
//
// The number of tile columns actually shown is a *step* function of
// that width, not continuous -- 3 minimum, stepping up to 4/5/6/... only
// once there's room for a whole extra column at minTileWidth. Below
// that threshold tiles simply grow to fill the newly available space at
// the current column count, the same way a file-explorer icon view or
// photo grid reflows: width changes smoothly, column count jumps.
QtObject {
    // Main.qml binds this once, off the same window-width signal that
    // drives UiScale.windowWidth.
    property real windowWidth: 1100

    // 320 / 1100 -- preserves today's default Browse column width (320,
    // at the app's own default 1100 startup width) exactly, so there's
    // no visual jump for a user who's never resized the window.
    readonly property real widthFraction: 320 / 1100

    // 64 matches the tile width today's hardcoded 4-column grid already
    // produces at the default window size (see columnsFor's own comment)
    // -- so column count at the default size resolves to 4, unchanged.
    readonly property int minTileWidth: 64
    readonly property int columnSpacing: 8
    readonly property int minColumns: 3

    // Estimated inset between the Browse column's own outer width (see
    // idealWidth below) and the actual inner width its tile grids
    // resolve to -- BrowseStack's own anchors.margins: 20 in Main.qml,
    // both sides. Only used for minimumColumnWidth/maxWidth's estimates
    // below; the grids themselves compute their real column count from
    // their own actual measured width (sectionsColumn.width in
    // BrowseHome.qml), not this estimate, so it never needs to be exact.
    readonly property int horizontalPadding: 40

    // Caps how far Browse grows in a very wide window -- past 6 columns
    // a tile grid stops reading as "usefully more content" and starts
    // reading as "oddly sparse", so growth just stops here instead
    // (Main.qml centers the whole 3-column group once every column has
    // hit its own cap like this one -- see its own maxContentWidth).
    readonly property int maxColumns: 6
    readonly property int maxWidth: maxColumns * minTileWidth + (maxColumns - 1) * columnSpacing + horizontalPadding

    readonly property int idealWidth: Math.min(maxWidth, Math.round(windowWidth * widthFraction))

    // The largest column count whose tiles would each be at least
    // minTileWidth wide within availableWidth -- derived the same way
    // BrowseHome.qml's own (now-removed) per-grid tileWidth math always
    // worked backwards from a fixed column count, just solved for
    // columns instead: N tiles at minTileWidth with (N-1) gaps fit iff
    // availableWidth >= N*minTileWidth + (N-1)*columnSpacing, i.e.
    // N <= (availableWidth + columnSpacing) / (minTileWidth + columnSpacing).
    function columnsFor(availableWidth) {
        return Math.max(minColumns,
                         Math.floor((availableWidth + columnSpacing) / (minTileWidth + columnSpacing)))
    }

    // Main.qml's browseColumn Layout.minimumWidth -- never narrower than
    // whatever minColumns needs at minTileWidth, so "no narrower than 3
    // icons per row" holds even at the app's own narrowest window size.
    readonly property int minimumColumnWidth:
        minColumns * minTileWidth + (minColumns - 1) * columnSpacing + horizontalPadding
}
