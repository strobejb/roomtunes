#pragma once

// Windows-only DWM / non-client-area chrome. Declarations are guarded so
// non-Windows translation units can include this header harmlessly.
//
// Ported from qexed's src/HexEdit/chrome/windows-chrome.h/.cpp -- same
// mechanism (collapse WM_NCCALCSIZE to keep DWM's native frame/shadow/
// rounded corners while hiding the title bar/border), adapted from a
// QMainWindow::nativeEvent() override to a QAbstractNativeEventFilter
// since QQuickWindow has no equivalent virtual to override.

// Q_OS_WIN is defined by qsystemdetection.h, pulled in transitively by any
// Qt header -- but this header intentionally includes none (so non-Windows
// TUs stay Qt-dependency-free), so without this the #ifdef below would
// never see the macro at all and silently compile out on every platform,
// Windows included.
#include <QtGlobal>

#ifdef Q_OS_WIN

class QWindow;

// Installs the WM_NCCALCSIZE-collapsing filter for this window and applies
// the DWM rounded-corner-preference and dark-mode attributes. Call once,
// after the window exists (winId() must be valid) but before first show.
void installWindowsChrome(QWindow *window);

// Re-applies DWMWA_USE_IMMERSIVE_DARK_MODE (e.g. after a theme change).
void updateWindowsDarkMode(QWindow *window, bool dark);

#endif // Q_OS_WIN
