/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "base/CmdLineArgs.h"
#include "base/File.h"
#include "base/Win.h"
#include "base/UITask.h"
#include "gui/Dpi.h"

#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/win/WinGui.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/VirtCtrl.h"
#include "gui/win/WebView.h"

#include "Settings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "AppSettings.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "SumatraPDF.h"
#include "Translations.h"
#include "Theme.h"
#include "DarkMode.h"
#include "SearchPanel.h"
#include "AIChatCommon.h"
#include "AIChatPanel.h"

struct SearchButtonState {
    bool isHovered = false;
    bool isTracking = false;
};

static SearchButtonState gBackState;
static SearchButtonState gForwardState;
static SearchButtonState gCloseState;

static COLORREF ColorToCOLORREF(Color c) {
    return RGB(GetRed(c), GetGreen(c), GetBlue(c));
}

static void PaintOwnerDrawButton(HWND hwnd, const WCHAR* label, bool isHovered, bool isCloseBtn) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    COLORREF bgCol;
    COLORREF txtCol = ColorToCOLORREF(ThemeWindowTextColor());

    if (isCloseBtn) {
        if (isHovered) {
            bgCol = RGB(240, 80, 20); // Orange hover effect
            txtCol = RGB(255, 255, 255);
        } else {
            bgCol = ColorToCOLORREF(ThemeControlBackgroundColor());
        }
    } else {
        if (isHovered) {
            bgCol = ColorToCOLORREF(AccentColor(ThemeControlBackgroundColor(), 30)); // Title bar button hover effect
        } else {
            bgCol = ColorToCOLORREF(ThemeControlBackgroundColor());
        }
    }

    HBRUSH brBg = CreateSolidBrush(bgCol);
    HPEN penBg = CreatePen(PS_SOLID, 1, bgCol);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, brBg);
    HPEN oldPen = (HPEN)SelectObject(hdc, penBg);

    int cornerRadius = DpiScale(8);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, cornerRadius, cornerRadius);

    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(brBg);
    DeleteObject(penBg);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, txtCol);
    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT oldFont = (HFONT)SelectObject(hdc, font);

    DrawTextW(hdc, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, oldFont);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK WndProcSearchBackBtn(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR subclassId, DWORD_PTR refData) {
    auto* win = (MainWindow*)refData;
    switch (msg) {
        case WM_MOUSEMOVE:
            if (!gBackState.isTracking) {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                gBackState.isTracking = true;
            }
            if (!gBackState.isHovered) {
                gBackState.isHovered = true;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        case WM_MOUSELEAVE:
            gBackState.isHovered = false;
            gBackState.isTracking = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            PaintOwnerDrawButton(hwnd, L"←", gBackState.isHovered, false);
            return 0;
        case WM_LBUTTONUP:
            if (win && win->webSearchWebView && win->webSearchWebView->CanGoBack()) {
                win->webSearchWebView->GoBack();
            }
            return 0;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK WndProcSearchForwardBtn(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR subclassId, DWORD_PTR refData) {
    auto* win = (MainWindow*)refData;
    switch (msg) {
        case WM_MOUSEMOVE:
            if (!gForwardState.isTracking) {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                gForwardState.isTracking = true;
            }
            if (!gForwardState.isHovered) {
                gForwardState.isHovered = true;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        case WM_MOUSELEAVE:
            gForwardState.isHovered = false;
            gForwardState.isTracking = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            PaintOwnerDrawButton(hwnd, L"→", gForwardState.isHovered, false);
            return 0;
        case WM_LBUTTONUP:
            if (win && win->webSearchWebView && win->webSearchWebView->CanGoForward()) {
                win->webSearchWebView->GoForward();
            }
            return 0;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK WndProcSearchCloseBtn(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR subclassId, DWORD_PTR refData) {
    auto* win = (MainWindow*)refData;
    switch (msg) {
        case WM_MOUSEMOVE:
            if (!gCloseState.isTracking) {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                gCloseState.isTracking = true;
            }
            if (!gCloseState.isHovered) {
                gCloseState.isHovered = true;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        case WM_MOUSELEAVE:
            gCloseState.isHovered = false;
            gCloseState.isTracking = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            PaintOwnerDrawButton(hwnd, L"×", gCloseState.isHovered, true);
            return 0;
        case WM_LBUTTONUP:
            if (win) {
                ReleaseCapture();
                WindowTab* tab = win->CurrentTab();
                if (tab) {
                    AIChatSetTabPanelOpen(tab, AIChatBackend::None);
                }
                AIChatSyncPanelsToCurrentTab(win);
                if (win->hwndCanvas) {
                    HwndSetFocus(win->hwndCanvas);
                } else if (win->hwndFrame) {
                    HwndSetFocus(win->hwndFrame);
                }
                ScheduleUiUpdate(win);
            }
            return 0;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static void OnWebSearchWebViewNavigated(void* ctx, Str, bool) {
    MainWindow* win = (MainWindow*)ctx;
    if (!IsMainWindowValidAndNotClosing(win) || !win->hwndAiChatBox) {
        return;
    }
    if (win->webSearchWebView) {
        win->webSearchWebView->SetControllerVisible(true);
        RelayoutSearchPanel(win);
    }
}

static void OnWebSearchHistoryChanged(void* ctx, bool, bool) {
    MainWindow* win = (MainWindow*)ctx;
    if (!IsMainWindowValidAndNotClosing(win) || !win->hwndAiChatBox) {
        return;
    }
    RelayoutSearchPanel(win);
}

void CreateSearchPanel(MainWindow* win) {
    if (!win) {
        return;
    }
    win->webSearchWebView = nullptr;
    win->webSearchWebViewReady = false;
}

void DestroySearchPanel(MainWindow* win) {
    if (!win) {
        return;
    }
    win->webSearchWebViewReady = false;
    if (win->hwndSearchBack) {
        DestroyWindow(win->hwndSearchBack);
        win->hwndSearchBack = nullptr;
    }
    if (win->hwndSearchForward) {
        DestroyWindow(win->hwndSearchForward);
        win->hwndSearchForward = nullptr;
    }
    if (win->hwndSearchClose) {
        DestroyWindow(win->hwndSearchClose);
        win->hwndSearchClose = nullptr;
    }
    delete win->webSearchWebView;
    win->webSearchWebView = nullptr;
}

void RelayoutSearchPanel(MainWindow* win) {
    if (!win || !win->webSearchWebView) {
        return;
    }
    Rect rc = HwndClientRect(win->hwndAiChatBox);
    if (rc.dx > 0 && rc.dy > 0) {
        // Full page search webview (0, 0, rc.dx, rc.dy) with zero top white bar
        MoveWindow(win->webSearchWebView->hwnd, 0, 0, rc.dx, rc.dy, TRUE);
        win->webSearchWebView->UpdateWebviewSize();

        bool isSearchTab = (win->activeSidebarTab == 1);
        int btnSize = DpiScale(28);
        int pad = DpiScale(8);
        int bottomY = rc.dy - btnSize - pad;

        bool canBack = win->webSearchWebView->CanGoBack();
        bool canFwd = win->webSearchWebView->CanGoForward();

        if (win->hwndSearchBack) {
            SetWindowPos(win->hwndSearchBack, HWND_TOP, pad, bottomY, btnSize, btnSize,
                         SWP_NOACTIVATE | (isSearchTab && canBack ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
            EnableWindow(win->hwndSearchBack, canBack);
            InvalidateRect(win->hwndSearchBack, nullptr, FALSE);
        }

        int fwdX = pad;
        if (canBack) {
            fwdX += btnSize + DpiScale(2);
        }
        if (win->hwndSearchForward) {
            SetWindowPos(win->hwndSearchForward, HWND_TOP, fwdX, bottomY, btnSize, btnSize,
                         SWP_NOACTIVATE | (isSearchTab && canFwd ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
            EnableWindow(win->hwndSearchForward, canFwd);
            InvalidateRect(win->hwndSearchForward, nullptr, FALSE);
        }

        if (win->hwndSearchClose) {
            SetWindowPos(win->hwndSearchClose, HWND_TOP, rc.dx - btnSize - pad, bottomY, btnSize, btnSize,
                         SWP_NOACTIVATE | (isSearchTab ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
            InvalidateRect(win->hwndSearchClose, nullptr, FALSE);
        }
    }
}

void OpenSearchSelectionInSidebar(MainWindow* win, Str engineName, Str url) {
    if (!win || !win->hwndAiChatBox || !HasWebView()) {
        return;
    }
    win->aiChatUsed = true;
    win->webSearchUsed = true;
    win->activeSidebarTab = 1;
    str::ReplaceWithCopy(&win->webSearchEngineName, engineName);
    win->uiState.aiChatVisible = true;

    if (!win->hwndSearchBack && win->hwndAiChatBox) {
        HINSTANCE hinst = GetModuleHandle(nullptr);
        win->hwndSearchBack = CreateWindowExW(0, WC_BUTTONW, L"←", WS_CHILD | BS_PUSHBUTTON, 0, 0, 0, 0, win->hwndAiChatBox, nullptr, hinst, nullptr);
        SetWindowSubclass(win->hwndSearchBack, WndProcSearchBackBtn, NextSubclassId(), (DWORD_PTR)win);

        win->hwndSearchForward = CreateWindowExW(0, WC_BUTTONW, L"→", WS_CHILD | BS_PUSHBUTTON, 0, 0, 0, 0, win->hwndAiChatBox, nullptr, hinst, nullptr);
        SetWindowSubclass(win->hwndSearchForward, WndProcSearchForwardBtn, NextSubclassId(), (DWORD_PTR)win);

        win->hwndSearchClose = CreateWindowExW(0, WC_BUTTONW, L"×", WS_CHILD | BS_PUSHBUTTON, 0, 0, 0, 0, win->hwndAiChatBox, nullptr, hinst, nullptr);
        SetWindowSubclass(win->hwndSearchClose, WndProcSearchCloseBtn, NextSubclassId(), (DWORD_PTR)win);
    }

    if (!win->webSearchWebView) {
        auto* webView = new WebviewWnd();
        webView->events.ctx = win;
        webView->events.navigationCompleted = OnWebSearchWebViewNavigated;
        webView->events.historyChanged = OnWebSearchHistoryChanged;
        TempStr localAppData = GetSpecialFolderTemp(CSIDL_LOCAL_APPDATA);
        TempStr safeName = str::ReplaceTemp(engineName, StrL(" "), StrL("_"));
        webView->dataDir = str::Dup(fmt("%s\\SumatraPDF\\Search_%s", localAppData, safeName));
        webView->allowClipboardRead = false;
        webView->defaultBackgroundColor = kColWhite;
        webView->forwardAppAccelerators = true;

        Rect rc = HwndClientRect(win->hwndAiChatBox);
        CreateWebViewArgs wvArgs;
        wvArgs.parent = win->hwndAiChatBox;
        wvArgs.pos = Rect(0, 0, rc.dx, rc.dy);
        webView->Create(wvArgs);
        if (webView->hwnd) {
            webView->Navigate(url);
            webView->SetIsVisible(true);
            win->webSearchWebView = webView;
            win->webSearchWebViewReady = true;
        } else {
            delete webView;
        }
    } else {
        win->webSearchWebView->Navigate(url);
        win->webSearchWebView->SetIsVisible(win->activeSidebarTab == 1);
    }
    RelayoutAIChatPanel(win);
    ScheduleUiUpdate(win);
}
