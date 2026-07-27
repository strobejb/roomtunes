pragma Singleton

import QtQuick

QtObject {
    // volume_x when muted; otherwise volume_0 below 5%, volume_1 below
    // 20%, and volume_2 above that. Ported from roomtunes-bb10's
    // Sonosjs.volumeIcon()/volumeSuffix(), collapsed to this app's
    // three-tier icon set.
    function nameFor(volume, muted) {
        if (muted)
            return "volume_x"
        if (volume < 5)
            return "volume_0"
        if (volume < 20)
            return "volume_1"
        return "volume_2"
    }
}
