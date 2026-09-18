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
    {gIconZoomIn, CmdZoomIn, "Zoom in"},
    {gIconZoomOut, CmdZoomOut, "Zoom out"},
    {gIconAnnotHighlight, CmdCreateAnnotHighlight, "Highlight"},
    {gIconAnnotInk, CmdCreateAnnotInk, "Ink"},
    {gIconAnnotText, CmdCreateAnnotText, "Text annotation"},
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

    Size GetIdealSize() override {
        return {sideLen, sideLen};
    }

    void Paint(VirtPaintCtx& ctx) override {
        if (IsEnabled() && HasFlag(vwfHovered) && hoverBg != kColorUnset) {
            ctx.gfx->FillRoundedRect(ctx.bounds, DpiScale(6), hoverBg);
        }
        VirtIconButton::Paint(ctx);
    }
};

static void OnFloatingButton(FloatingToolbar* tb, VirtMouseEvent* ev) {
    if (!tb || !ev || !ev->target) {
        return;
    }
    int cmd = ev->target->id;
    if (cmd) {
        HwndPostCommand(tb->win->hwndFrame, cmd, 0);
    }
}

static void PaintFloatingToolbar(FloatingToolbar*, VirtHostPaintEvent* ev) {
    ev->gfx->FillRoundedRect(ev->clientRect, DpiScale(kFloatingToolbarRadius), FloatingBg(), FloatingBorder());
}

static void MoveFloatingToolbar(FloatingToolbar* tb, Rect r) {
    if (!tb || !tb->host || r == tb->lastToolbarRect) {
        return;
    }
    tb->lastToolbarRect = r;
    tb->host->SetBounds(r);
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

    int x = fr.x + DpiScale(8);
    int y = fr.y + std::max(DpiScale(70), (fr.dy - h) / 2);
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
