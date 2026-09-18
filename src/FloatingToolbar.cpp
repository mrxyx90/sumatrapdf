/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "base/Win.h"
#include "gui/Dpi.h"
#include "base/Pixmap.h"

#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/GuiColors.h"
#include "gui/VirtCtrl.h"
#include "gui/VirtHost.h"
#include "SvgIcons.h"
#include "Commands.h"
#include "Settings.h"
#include "AppSettings.h"
#include "MainWindow.h"
#include "FloatingToolbar.h"
#include "Theme.h"

constexpr const WCHAR* kFloatingToolbarClassName = L"SumatraFloatingToolbar";
constexpr int kFloatingToolbarIconSize = 22;
constexpr int kFloatingToolbarButtonSize = 38;
constexpr int kFloatingToolbarMargin = 5;
constexpr int kFloatingToolbarGap = 2;
constexpr int kFloatingToolbarRadius = 9;

struct FloatingToolbarButton {
    const char* icon = nullptr;
    int cmdId = 0;
    const char* tip = nullptr;
};

static const FloatingToolbarButton gButtons[] = {
    {gIconCommandPalette, CmdCommandPalette, "Command palette"},
    // Use the brush/highlighter glyph, not the selection-toolbar text-marking
    // glyph, so this button is visually the highlighter tool.
    {gIconAnnotHighlightBrush, CmdCreateAnnotHighlight, "Highlight"},
    {gIconAnnotInk, CmdCreateAnnotInk, "Ink"},
    {gIconAnnotFreeText, CmdCreateAnnotFreeText, "Free text"},
    {gIconEditAnnotations, CmdToggleEditPDF, "Edit PDF"},
};

struct FloatingToolbar {
    MainWindow* win = nullptr;
    VirtHost* host = nullptr;
    Rect lastFrameRect;
    Rect lastToolbarRect;
    bool dragging = false;
    POINT dragStart{};
    Rect dragOrig;
    int activeCmdId = 0;
};

static Color FloatingBg() {
    return ThemeControlBackgroundColor();
}

static Color FloatingBorder() {
    return ThemeEdgeColor();
}

static Color FloatingHover() {
    return ThemeHotBackgroundColor();
}

struct FloatingIconButton : VirtIconButton {
    int sideLen = 0;
    Color hoverBg = kColorUnset;
    FloatingToolbar* toolbar = nullptr;

    Size GetIdealSize() override {
        return {sideLen, sideLen};
    }

    void Paint(VirtPaintCtx& ctx) override {
        if (IsEnabled() && HasFlag(vwfHovered) && hoverBg != kColorUnset) {
            ctx.gfx->FillRoundedRect(ctx.bounds, DpiScale(6), hoverBg);
        }
        VirtIconButton::Paint(ctx);

        if (toolbar && toolbar->activeCmdId == id) {
            // Keep the active-tool indication as a blue border only.
            ctx.gfx->DrawRect(ctx.bounds, 0xff0078d4, DpiScale(2));
        }
    }
};

static void OnFloatingButton(FloatingToolbar* tb, VirtMouseEvent* ev) {
    if (!tb || !ev || !ev->target) {
        return;
    }
    int cmd = ev->target->id;
    if (!cmd) {
        return;
    }

    // Tool buttons toggle their blue selection border. Clicking an already
    // selected tool deselects it; clicking another tool selects that one.
    if (tb->activeCmdId == cmd) {
        tb->activeCmdId = 0;
    } else {
        tb->activeCmdId = cmd;
    }
    tb->host->Invalidate(false);

    if (cmd == CmdCreateAnnotHighlight || cmd == CmdCreateAnnotUnderline || cmd == CmdCreateAnnotSquiggly ||
        cmd == CmdCreateAnnotStrikeOut) {
        HwndSendCommand(tb->win->hwndFrame, cmd, 0);
        return;
    }
    HwndPostCommand(tb->win->hwndFrame, cmd, 0);
}

static void PaintFloatingToolbar(FloatingToolbar*, VirtHostPaintEvent* ev) {
    ev->gfx->FillRoundedRect(ev->clientRect, DpiScale(kFloatingToolbarRadius), FloatingBg(), FloatingBorder());
}

static int FloatingToolbarSidebarOffset(FloatingToolbar* tb) {
    if (!tb || !tb->win || !tb->win->hwndTocBox || !IsWindowVisible(tb->win->hwndTocBox)) {
        return 0;
    }
    return tb->win->sidebarDx + DpiScale(8);
}

static bool FloatingToolbarIsForPdf(FloatingToolbar* tb) {
    return tb && tb->win && tb->win->IsDocLoaded() && tb->win->AsFixed() != nullptr;
}

static void MoveFloatingToolbar(FloatingToolbar* tb, Rect r) {
    if (!tb || !tb->host) {
        return;
    }
    tb->lastToolbarRect = r;
    // Keep this popup above the document and sidebar child windows. The
    // bookmark sidebar can be created/reordered after the toolbar at startup.
    SetWindowPos(tb->host->native, HWND_TOP, r.x, r.y, r.dx, r.dy, SWP_NOACTIVATE);
}

static void PositionFloatingToolbar(FloatingToolbar* tb) {
    if (!tb || !tb->host) {
        return;
    }

    RECT frame{};
    GetWindowRect(tb->win->hwndFrame, &frame);
    Rect fr(frame.left, frame.top, frame.right - frame.left, frame.bottom - frame.top);
    if (fr.dx <= 0 || fr.dy <= 0) {
        return;
    }

    int w = DpiScale(2 * kFloatingToolbarMargin + kFloatingToolbarButtonSize);
    int h = DpiScale(2 * kFloatingToolbarMargin +
                     (int)dimof(gButtons) * kFloatingToolbarButtonSize +
                     ((int)dimof(gButtons) - 1) * kFloatingToolbarGap);

    if (!FloatingToolbarIsForPdf(tb)) {
        ShowWindow(tb->host->native, SW_HIDE);
        return;
    }
    ShowWindow(tb->host->native, SW_SHOWNOACTIVATE);

    int x = fr.x + DpiScale(8);
    int y = fr.y + std::max(DpiScale(70), (fr.dy - h) / 2);

    // A non-zero saved position is an explicit user placement.
    if (gSettings && (gSettings->floatingToolbarPosition.x != 0 || gSettings->floatingToolbarPosition.y != 0)) {
        x = gSettings->floatingToolbarPosition.x;
        y = gSettings->floatingToolbarPosition.y;

        int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (vw > w) {
            x = std::clamp(x, vx, vx + vw - w);
        }
        if (vh > h) {
            y = std::clamp(y, vy, vy + vh - h);
        }
    }
    if (tb->win->hwndTocBox && IsWindowVisible(tb->win->hwndTocBox)) {
        // Only move the toolbar if its saved position is in the bookmark
        // sidebar area. A toolbar placed elsewhere must not be affected by
        // opening the sidebar.
        int savedX = gSettings ? gSettings->floatingToolbarPosition.x : 0;
        bool toolbarInSidebar = savedX != 0 && savedX < fr.x + tb->win->sidebarDx;
        if (toolbarInSidebar) {
            x = fr.x + tb->win->sidebarDx + DpiScale(8);
        }
    }
    MoveFloatingToolbar(tb, {x, y, w, h});
    tb->lastFrameRect = fr;
}

static void OnFloatingNativeMsg(FloatingToolbar* tb, VirtHostNativeMsg* ev) {
    if (!tb || !ev) {
        return;
    }

    switch (ev->msg) {
    case WM_LBUTTONDOWN: {
        Point p(GET_X_LPARAM(ev->lp), GET_Y_LPARAM(ev->lp));
        ILayout* hit = ElementFromPoint(tb->host->vroot, p);
        VirtCtrl* hitCtrl = hit ? hit->AsVirtCtrl() : nullptr;
        bool onButton = hitCtrl && hitCtrl->id != 0;
        if (!onButton) {
            GetCursorPos(&tb->dragStart);
            tb->dragging = true;
            tb->dragOrig = tb->host->ScreenRect();
            SetCapture(tb->host->native);
            ev->didHandle = true;
        }
        break;
    }
    case WM_MOUSEMOVE:
        if (tb->dragging) {
            POINT screen{};
            GetCursorPos(&screen);
            int dx = screen.x - tb->dragStart.x;
            int dy = screen.y - tb->dragStart.y;
            MoveFloatingToolbar(tb, {tb->dragOrig.x + dx, tb->dragOrig.y + dy,
                                     tb->dragOrig.dx, tb->dragOrig.dy});
            ev->didHandle = true;
        }
        break;
    case WM_LBUTTONUP:
        if (tb->dragging) {
            tb->dragging = false;
            ReleaseCapture();
            if (gSettings) {
                Rect r = tb->host->ScreenRect();
                int sidebarOffset = FloatingToolbarSidebarOffset(tb);
                gSettings->floatingToolbarPosition.x = r.x - sidebarOffset;
                gSettings->floatingToolbarPosition.y = r.y;
                ScheduleSaveSettings();
                FlushScheduledSaveSettings();
            }
            ev->didHandle = true;
        }
        break;
    case WM_CAPTURECHANGED:
        tb->dragging = false;
        break;
    }
}

static void BuildFloatingToolbar(FloatingToolbar* tb) {
    auto* box = new VBox();
    box->alignCross = CrossAxisAlign::Stretch;

    int iconSize = DpiScale(kFloatingToolbarIconSize);
    int buttonSize = DpiScale(kFloatingToolbarButtonSize);
    int gap = DpiScale(kFloatingToolbarGap);

    for (int i = 0; i < (int)dimof(gButtons); i++) {
        const auto& b = gButtons[i];
        auto* button = new FloatingIconButton();
        button->sideLen = buttonSize;
        button->hoverBg = FloatingHover();
        button->toolbar = tb;
        button->pixmap = GetCachedPixmapForSvg(Str(b.icon), iconSize, iconSize,
                                                ThemeWindowTextColor(), FloatingBg());
        button->SetTooltip(Str(b.tip));
        button->id = b.cmdId;
        button->onClick = MkFunc1(OnFloatingButton, tb);
        box->AddChild(button);
        if (i + 1 != (int)dimof(gButtons)) {
            box->AddChild(new Spacer(gap, gap));
        }
    }

    auto* content = new Padding(box, Insets{kFloatingToolbarMargin, kFloatingToolbarMargin,
                                             kFloatingToolbarMargin, kFloatingToolbarMargin});
    tb->host->SetLayoutSizedToContent(content);
}

void FloatingToolbarCreate(MainWindow* win) {
    if (!win || win->floatingToolbar) {
        return;
    }

    auto* tb = new FloatingToolbar();
    tb->win = win;

    VirtHost::CreateArgs args;
    args.parent = win->hwndFrame;
    args.className = WStr(kFloatingToolbarClassName);
    args.isPopup = true;
    args.visible = true;
    args.noActivate = true;
    args.clipSiblings = true;
    args.userData = tb;

    tb->host = VirtHost::Create(args);
    if (!tb->host) {
        delete tb;
        return;
    }

    tb->host->onPaintBackground = MkFunc1(PaintFloatingToolbar, tb);
    tb->host->onNativeMsg = MkFunc1(OnFloatingNativeMsg, tb);
    BuildFloatingToolbar(tb);
    win->floatingToolbar = tb;
    win->floatingToolbarOnWindowMoved = MkFunc1Void(FloatingToolbarOnWindowMoved);
    win->RegisterOnWindowMoved(&win->floatingToolbarOnWindowMoved);

    PositionFloatingToolbar(tb);
}

void FloatingToolbarUpdateTheme() {
    for (int i = 0; i < len(gWindows); i++) {
        MainWindow* win = gWindows[i];
        FloatingToolbar* tb = win ? win->floatingToolbar : nullptr;
        if (!tb || !tb->host) {
            continue;
        }

        BuildFloatingToolbar(tb);
        PositionFloatingToolbar(tb);
        tb->host->Invalidate(false);
    }
}

void FloatingToolbarDestroy(MainWindow* win) {
    if (!win || !win->floatingToolbar) {
        return;
    }
    auto* tb = win->floatingToolbar;
    win->UnregisterOnWindowMoved(&win->floatingToolbarOnWindowMoved);
    delete tb->host;
    tb->host = nullptr;
    delete tb;
    win->floatingToolbar = nullptr;
}

void FloatingToolbarRelayout(MainWindow* win) {
    if (!win || !win->floatingToolbar) {
        return;
    }
    PositionFloatingToolbar(win->floatingToolbar);
}

void FloatingToolbarOnWindowMoved(MainWindow* win) {
    if (!win || !win->floatingToolbar) {
        return;
    }

    auto* tb = win->floatingToolbar;
    if (tb->dragging) {
        return;
    }

    RECT rc{};
    GetWindowRect(win->hwndFrame, &rc);
    Rect frame(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
    if (tb->lastFrameRect.dx <= 0 || tb->lastFrameRect.dy <= 0) {
        PositionFloatingToolbar(tb);
        return;
    }

    int dx = frame.x - tb->lastFrameRect.x;
    int dy = frame.y - tb->lastFrameRect.y;
    if (dx || dy) {
        Rect current = tb->host->ScreenRect();
        MoveFloatingToolbar(tb, {current.x + dx, current.y + dy, current.dx, current.dy});
    }
    tb->lastFrameRect = frame;
}
