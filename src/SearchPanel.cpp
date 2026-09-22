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

void CreateSearchPanel(MainWindow* win) {
    win->webSearchWebView = nullptr;
    win->webSearchWebViewReady = false;
}

void DestroySearchPanel(MainWindow* win) {
    win->webSearchWebViewReady = false;
    delete win->webSearchWebView;
    win->webSearchWebView = nullptr;
}

void RelayoutSearchPanel(MainWindow* win) {
    if (!win || !win->webSearchWebView) {
        return;
    }
    Rect rc = HwndClientRect(win->hwndAiChatBox);
    if (rc.dx > 0 && rc.dy > 0) {
        MoveWindow(win->webSearchWebView->hwnd, 0, 0, rc.dx, rc.dy, TRUE);
        win->webSearchWebView->UpdateWebviewSize();
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

    if (!win->webSearchWebView) {
        auto* webView = new WebviewWnd();
        webView->events.ctx = win;
        webView->events.navigationCompleted = OnWebSearchWebViewNavigated;
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
    RelayoutSearchPanel(win);
    ScheduleUiUpdate(win);
}
