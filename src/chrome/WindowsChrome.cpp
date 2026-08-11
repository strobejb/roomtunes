//
// WindowsChrome.cpp -- DWM / non-client-area chrome for Windows, Quick edition.
//
// Ported from qexed's src/HexEdit/chrome/windows-chrome.cpp, which does this
// for a QMainWindow via QMainWindow::nativeEvent(). QQuickWindow has no such
// virtual to override, so the same WM_NCCALCSIZE-collapsing logic is done
// here via a QAbstractNativeEventFilter installed on the application instead.
//

#include "WindowsChrome.h"

#ifdef Q_OS_WIN

#include <QAbstractNativeEventFilter>
#include <QGuiApplication>
#include <QPalette>
#include <QWindow>

#include <dwmapi.h>
#include <windows.h>

// Windows 11 SDK attributes -- define locally so the build works with older
// SDKs / MinGW headers that don't yet declare them (same fallback qexed uses
// in windows-chrome.h:14-23).
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#define DWMWCP_ROUND 2
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace
{

// Collapses the non-client area to zero for one specific HWND: the title
// bar/border chrome never appears, but DWM still sees WS_THICKFRAME and
// keeps applying its rounded corners, accent border, and drop-shadow.
// Ported from qexed's MainWindow::nativeEvent (windows-chrome.cpp:198-222).
class NcCalcSizeFilter : public QAbstractNativeEventFilter
{
  public:
    explicit NcCalcSizeFilter(HWND hwnd) : m_hwnd(hwnd)
    {
    }

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override
    {
        // Confirmed empirically against Qt 6.10.3/llvm-mingw: Qt delivers
        // both "windows_generic_MSG" and "windows_dispatcher_MSG" for the
        // same message; either carries a MSG* here.
        if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")
            return false;

        MSG *msg = reinterpret_cast<MSG *>(message);
        if (msg->hwnd != m_hwnd || msg->message != WM_NCCALCSIZE || msg->wParam != TRUE)
            return false;

        // When maximized, Windows extends the window rect slightly
        // off-screen (by the frame thickness) to hide the thick-frame
        // border. Without compensation the client area would bleed under
        // the taskbar. Trim it back by the DPI-aware frame size.
        if (IsZoomed(msg->hwnd))
        {
            auto *params = reinterpret_cast<NCCALCSIZE_PARAMS *>(msg->lParam);
            UINT  dpi    = GetDpiForWindow(msg->hwnd);
            int   bx     = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            int   by     = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            params->rgrc[0].left   += bx;
            params->rgrc[0].right  -= bx;
            params->rgrc[0].top    += by;
            params->rgrc[0].bottom -= by;
        }

        if (result)
            *result = 0;
        return true;
    }

  private:
    HWND m_hwnd;
};

} // namespace

void installWindowsChrome(QWindow *window)
{
    HWND hwnd = reinterpret_cast<HWND>(window->winId());

    // Rounded corners -- Win11 22000+ enables this for a normal top-level
    // window by default, but be explicit so nothing can reset it.
    DWORD cornerPref = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));

    updateWindowsDarkMode(window, QGuiApplication::palette().window().color().lightness() < 128);

    // Lifetime: intentionally leaked. RoomTunes has exactly one top-level
    // window for its whole process lifetime, so this filter needs to live
    // until the QGuiApplication itself is torn down -- no earlier moment
    // is correct to delete it.
    qApp->installNativeEventFilter(new NcCalcSizeFilter(hwnd));

    // WM_NCCALCSIZE only fires on creation (before this filter existed) and
    // on a subsequent resize/move/frame-change -- not on plain ShowWindow.
    // Without a nudge here nothing ever asks Windows to recalculate the
    // frame again, so the filter would sit installed but never fire until
    // the user manually resized the window. SWP_FRAMECHANGED forces that
    // recalculation immediately (same trick windows-chrome.cpp uses in
    // applyDwmDarkMode to force an immediate title-bar repaint).
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void updateWindowsDarkMode(QWindow *window, bool dark)
{
    HWND       hwnd = reinterpret_cast<HWND>(window->winId());
    const BOOL val  = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &val, sizeof(val));
}

#endif // Q_OS_WIN
