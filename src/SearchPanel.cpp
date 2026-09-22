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
#include "AIChatPanel.h"

static LRESULT CALLBACK WndProcSearchBackBtn(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR subclassId, DWORD_PTR refData) {
    auto* win = (MainWindow*)refData;
    if (msg == WM_LBUTTONUP) {
        if (win && win->webSearchWebView && win->webSearchWebView->CanGoBack()) {
            win->webSearchWebView->GoBack();
        }
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK WndProcSearchForwardBtn(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR subclassId, DWORD_PTR refData) {
    auto* win = (MainWindow*)refData;
    if (msg == WM_LBUTTONUP) {
        if (win && win->webSearchWebView && win->webSearchWebView->CanGoForward()) {
            win->webSearchWebView->GoForward();
        }
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK WndProcSearchCloseBtn(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR subclassId, DWORD_PTR refData) {
    auto* win = (MainWindow*)refData;
    if (msg == WM_LBUTTONUP) {
        if (win) {
            win->uiState.aiChatVisible = false;
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
        int btnSize = DpiScale(26);
        int pad = DpiScale(4);

        bool canBack = win->webSearchWebView->CanGoBack();
        bool canFwd = win->webSearchWebView->CanGoForward();

        if (win->hwndSearchBack) {
            SetWindowPos(win->hwndSearchBack, HWND_TOP, pad, pad, btnSize, btnSize,
                         SWP_NOACTIVATE | (isSearchTab && canBack ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
            EnableWindow(win->hwndSearchBack, canBack);
        }

        int fwdX = pad;
        if (canBack) {
            fwdX += btnSize + pad;
        }
        if (win->hwndSearchForward) {
            SetWindowPos(win->hwndSearchForward, HWND_TOP, fwdX, pad, btnSize, btnSize,
                         SWP_NOACTIVATE | (isSearchTab && canFwd ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
            EnableWindow(win->hwndSearchForward, canFwd);
        }

        if (win->hwndSearchClose) {
            SetWindowPos(win->hwndSearchClose, HWND_TOP, rc.dx - btnSize - pad, pad, btnSize, btnSize,
                         SWP_NOACTIVATE | (isSearchTab ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
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
