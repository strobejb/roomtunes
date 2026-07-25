#pragma once

#include <QColor>
#include <QImage>

namespace RoomTunes {

// Picks one prominent, saturated color from an image's histogram -- biased
// toward whichever distinct hue appears most often, excluding near-white/
// near-black/near-grey pixels so the result reads as an actual accent color
// rather than washed-out noise. Used to derive the Now Playing panel's
// per-track background in "color" render mode.
class AlbumColorAnalyzer
{
public:
    static QColor pickAccentColor(const QImage &image);
};

}
