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
#include "AnnotPlacement.h"
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
    {gIconAnnotHighlightBrush, CmdAnnotationHighlightBrush, "Highlight"},
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

    // Tool buttons toggle their blue selection border and placement mode.
    // Clicking the selected placement tool again cancels the active mode.
    if (tb->activeCmdId == cmd) {
        CancelAnnotationPlacement(tb->win);
        tb->activeCmdId = 0;
        tb->host->Invalidate(false);
        return;
    }

    tb->activeCmdId = cmd;
    tb->host->Invalidate(false);

    if (cmd == CmdAnnotationHighlightBrush || cmd == CmdCreateAnnotInk || cmd == CmdCreateAnnotFreeText ||
        cmd == CmdCreateAnnotUnderline || cmd == CmdCreateAnnotSquiggly || cmd == CmdCreateAnnotStrikeOut) {
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

    int x = fr.x + DpiScale(4);
    int y = fr.y + DpiScale(14);

    // When the application is resized, keep the toolbar anchored to the same
    // horizontal side of the frame. A toolbar in the left half keeps its
    // distance from the left edge; one in the right half keeps its distance
    // from the right edge.
    bool frameWasResized = tb->lastFrameRect.dx > 0 && tb->lastFrameRect.dy > 0 &&
                           (tb->lastFrameRect.dx != fr.dx || tb->lastFrameRect.dy != fr.dy);
    if (frameWasResized && tb->lastToolbarRect.dx > 0) {
        int oldFrameCenter = tb->lastFrameRect.x + tb->lastFrameRect.dx / 2;
        int oldToolbarCenter = tb->lastToolbarRect.x + tb->lastToolbarRect.dx / 2;
        if (oldToolbarCenter <= oldFrameCenter) {
            x = fr.x + (tb->lastToolbarRect.x - tb->lastFrameRect.x);
        } else {
            int oldRightMargin = tb->lastFrameRect.x + tb->lastFrameRect.dx -
                                 (tb->lastToolbarRect.x + tb->lastToolbarRect.dx);
            x = fr.x + fr.dx - w - oldRightMargin;
        }
    }

    // Restore the saved position relative to the frame. The saved X value
    // is a left offset for a toolbar in the left half and a right offset for
    // a toolbar in the right half. Y is always an offset from the top edge.
    if (!frameWasResized && gSettings &&
        (gSettings->floatingToolbarPosition.x != 0 || gSettings->floatingToolbarPosition.y != 0)) {
        int savedX = gSettings->floatingToolbarPosition.x;
        int savedY = gSettings->floatingToolbarPosition.y;
        if (savedX >= 0) {
            x = fr.x + savedX;
        } else {
            int rightOffset = -savedX;
            x = fr.x + fr.dx - w - rightOffset;
        }
        y = fr.y + savedY;
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

    // Clamp after applying the sidebar position too. A wide sidebar or a
    // restored position must never allow the popup outside the frame.
    int minX = fr.x + DpiScale(4);
    int maxX = std::max(minX, fr.x + fr.dx - w - DpiScale(4));
    int minY = fr.y + DpiScale(14);
    int maxY = std::max(minY, fr.y + fr.dy - h);
    x = std::clamp(x, minX, maxX);
    y = std::clamp(y, minY, maxY);
    MoveFloatingToolbar(tb, {x, y, w, h});

    // Persist the position relative to the current frame. Positive X stores
    // the left offset; negative X stores the right offset. Y always stores
    // the offset from the top edge. Do this after every layout/resize so the
    // latest anchored position is restored on the next launch.
    if (gSettings) {
        int frameCenter = fr.x + fr.dx / 2;
        int toolbarCenter = x + w / 2;
        int sidebarOffset = FloatingToolbarSidebarOffset(tb);
        if (toolbarCenter <= frameCenter) {
            gSettings->floatingToolbarPosition.x = x - fr.x - sidebarOffset;
        } else {
            int rightOffset = fr.x + fr.dx - (x + w);
            gSettings->floatingToolbarPosition.x = -(rightOffset + sidebarOffset);
        }
        gSettings->floatingToolbarPosition.y = y - fr.y;
        if (frameWasResized || tb->lastToolbarRect.x != x || tb->lastToolbarRect.y != y) {
            ScheduleSaveSettings();
            FlushScheduledSaveSettings();
        }
    }

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
            RECT frame{};
            GetWindowRect(tb->win->hwndFrame, &frame);
            int minX = frame.left;
            int minY = frame.top;
            int maxX = std::max<int>(minX, frame.right - tb->dragOrig.dx);
            int maxY = std::max<int>(minY, frame.bottom - tb->dragOrig.dy);
            int x = std::clamp(tb->dragOrig.x + dx, minX, maxX);
            int y = std::clamp(tb->dragOrig.y + dy, minY, maxY);
            MoveFloatingToolbar(tb, {x, y, tb->dragOrig.dx, tb->dragOrig.dy});
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
