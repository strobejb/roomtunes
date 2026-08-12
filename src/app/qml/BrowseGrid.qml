pragma Singleton
import QtQuick

// Shared sizing policy for the Browse column's icon-tile grids
// (BrowseHome.qml's Recently Played/Favourites/Your Services sections).
// The column's own outer width (see idealWidth below, read by Main.qml's
// browseColumn) aims at a proportion of window.width, exactly like
// zonesColumn's UiScale.factor -- but unlike Zones (which only ever shrinks
// from its 280 baseline), Browse can also grow past its baseline, since more
// whole tile columns genuinely help a tile grid.
//
// The outer width and tile count are both snapped: 3 minimum, stepping up
// to 4/5/6 only once the proportional target has room for another whole
// tile. Tiles keep a fixed width; spare window space goes to the other
// columns or the centered outer margins instead of stretching Browse's icon
// spacing.
QtObject {
    // Main.qml binds this once, off the same window-width signal that
    // drives UiScale.windowWidth.
    property real windowWidth: 1100

    // 320 / 1100 -- preserves today's default Browse column width (320,
    // at the app's own default 1100 startup width) exactly, so there's
    // no visual jump for a user who's never resized the window.
    readonly property real widthFraction: 320 / 1100

    // Keeps the default 1100px window at 4 columns: 4*66 + 3*8 plus the
    // BrowseStack side margins below resolves to 318px, close to the old
    // continuous 320px target.
    readonly property int tileWidth: 66
    readonly property int columnSpacing: 8
    readonly property int minColumns: 3
    readonly property int maxColumns: 6

    // Inset between the Browse column's own outer width (see idealWidth
    // below) and the actual inner width its tile grids resolve to --
    // BrowseStack's 15px left/right margins in Main.qml.
    readonly property int horizontalPadding: 30

    function innerWidthFor(columns) {
        return columns * tileWidth + (columns - 1) * columnSpacing
    }

    function outerWidthFor(columns) {
        return innerWidthFor(columns) + horizontalPadding
    }

    function columnsForTarget(targetWidth) {
        return Math.max(minColumns,
                        Math.min(maxColumns,
                                 Math.floor((targetWidth - horizontalPadding + columnSpacing)
                                            / (tileWidth + columnSpacing))))
    }

    // Caps how far Browse grows in a very wide window -- past 6 columns
    // a tile grid stops reading as "usefully more content".
    readonly property int maxWidth: outerWidthFor(maxColumns)

    readonly property int idealColumns: columnsForTarget(Math.round(windowWidth * widthFraction))
    readonly property int idealWidth: outerWidthFor(idealColumns)

    // The largest column count whose tiles would each be at least
    // tileWidth wide within availableWidth -- derived the same way
    // BrowseHome.qml's own (now-removed) per-grid tileWidth math always
    // worked backwards from a fixed column count, just solved for
    // columns instead: N tiles at tileWidth with (N-1) gaps fit iff
    // availableWidth >= N*tileWidth + (N-1)*columnSpacing, i.e.
    // N <= (availableWidth + columnSpacing) / (tileWidth + columnSpacing).
    function columnsFor(availableWidth) {
        return Math.max(minColumns,
                        Math.min(maxColumns,
                                 Math.floor((availableWidth + columnSpacing) / (tileWidth + columnSpacing))))
    }

    // Main.qml's browseColumn Layout.minimumWidth -- never narrower than
    // whatever minColumns needs at tileWidth, so "no narrower than 3
    // icons per row" holds even at the app's own narrowest window size.
    readonly property int minimumColumnWidth: outerWidthFor(minColumns)
}
