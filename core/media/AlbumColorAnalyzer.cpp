#include "AlbumColorAnalyzer.h"

#include <QHash>

namespace RoomTunes
{

namespace
{

// One pass over the (already downscaled) image, bucketing pixels that pass
// the given saturation/value floors into a coarse RGB histogram, and
// returning the most frequent bucket's representative color. Returns an
// invalid QColor if nothing passes the floors at all.
QColor pickWithThresholds(const QImage &scaled, qreal minSaturation, qreal minValue)
{
    QHash<quint32, int>    histogram;
    QHash<quint32, QColor> representative;

    for (int y = 0; y < scaled.height(); ++y)
    {
        for (int x = 0; x < scaled.width(); ++x)
        {
            const QColor c = scaled.pixelColor(x, y);
            float        h, s, v;
            c.getHsvF(&h, &s, &v);

            if (v < minValue || s < minSaturation)
                continue; // too dark, or too washed-out/grey to read as "a color"

            // Quantize into coarse buckets so near-identical hues (e.g.
            // antialiasing/JPEG noise around one dominant color) count as
            // the same color instead of splitting frequency across dozens
            // of 1-off RGB values.
            const int     r   = (c.red() / 24) * 24;
            const int     g   = (c.green() / 24) * 24;
            const int     b   = (c.blue() / 24) * 24;
            const quint32 key = (quint32(r) << 16) | (quint32(g) << 8) | quint32(b);

            histogram[key]++;
            if (!representative.contains(key))
                representative.insert(key, QColor(r, g, b));
        }
    }

    if (histogram.isEmpty())
        return {};

    quint32 bestKey   = 0;
    int     bestCount = -1;
    for (auto it = histogram.constBegin(); it != histogram.constEnd(); ++it)
    {
        if (it.value() > bestCount)
        {
            bestCount = it.value();
            bestKey   = it.key();
        }
    }

    return representative.value(bestKey);
}

} // namespace

QColor AlbumColorAnalyzer::pickAccentColor(const QImage &image)
{
    if (image.isNull())
        return {};

    const QImage scaled =
        image.scaled(48, 48, Qt::IgnoreAspectRatio, Qt::FastTransformation).convertToFormat(QImage::Format_RGB32);

    // Progressively relax the "must look like a real color" floors if
    // nothing passes -- covers mostly grayscale/monochrome artwork, which
    // would otherwise yield no candidates at all under strict thresholds.
    const QColor strict = pickWithThresholds(scaled, 0.35, 0.20);
    if (strict.isValid())
        return strict;

    const QColor relaxed = pickWithThresholds(scaled, 0.18, 0.12);
    if (relaxed.isValid())
        return relaxed;

    const QColor loose = pickWithThresholds(scaled, 0.08, 0.08);
    if (loose.isValid())
        return loose;

    // Genuinely no usable color (near-solid black/white/grey artwork) --
    // a fixed, pleasant fallback rather than an invalid/undefined color.
    return QColor(0x5B, 0x3A, 0x8E);
}

} // namespace RoomTunes
