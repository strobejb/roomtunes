pragma Singleton
import QtQuick

// App-wide scale factor for "headline" text -- panel titles (Zones/
// Browse/Queue and their sub-page titles), zone card room names, and
// track/folder item titles -- plus the Zones column's own width. Both
// shrink together as the window narrows, so the left-hand side of the
// app reads as one coherent, proportionally-shrunk layout instead of
// each piece hitting its own separate breakpoint.
//
// Deliberately NOT read by NowPlayingWide/NowPlayingCompact -- those
// already have their own dedicated narrow-panel handling (see
// NowPlayingCompact.qml's sizeScale) -- and not by any other text in the
// app (artist names, timestamps, captions, ...), which is small enough
// already that shrinking it further would hurt legibility rather than
// help it.
QtObject {
    // Main.qml binds this once to the ApplicationWindow's actual width;
    // everything else just reads factor below.
    property real windowWidth: 1100

    // Also read directly by Main.qml's own window.minimumWidth
    // calculation, so the OS-level resize floor and the point where
    // this factor itself bottoms out always agree on the same number.
    readonly property real minFactor: 0.75

    // 1100 is Main.qml's own default startup width, so factor is 1 (no
    // shrink at all) at or above that -- a user who's never resized the
    // window sees exactly the original sizes. Floored at minFactor so
    // headline text and the Zones column never shrink past clearly
    // readable/usable.
    readonly property real factor: Math.max(minFactor, Math.min(1.0, windowWidth / 1100))
}
