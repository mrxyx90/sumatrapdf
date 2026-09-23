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
#include "SumatraPDF.h"
#include "FloatingToolbar.h"
#include "ScreenshotCapture.h"
#include "AnnotPlacement.h"
#include "Theme.h"
#include "Notifications.h"
#include "Toolbar.h"
#include "CommandPalette.h"

constexpr const WCHAR* kFloatingToolbarClassName = L"SumatraFloatingToolbar";
constexpr int kFloatingToolbarIconSize = 22;
constexpr int kFloatingToolbarButtonSize = 38;
constexpr int kFloatingToolbarMargin = 5;
constexpr int kFloatingToolbarGap = 2;
constexpr int kFloatingToolbarRadius = 9;
constexpr int kFloatingToolbarSeparatorGap = 5;
// hover dropdown timer IDs
constexpr int kFloatingToolbarOpenHoverDropdownTimerId = 0x201;
constexpr int kFloatingToolbarCloseHoverDropdownTimerId = 0x202;

struct FloatingToolbarButton {
    const char* icon = nullptr;
    int cmdId = 0;
    const char* tip = nullptr;
};

static constexpr const char* kScreenshotIcon =
    "<svg viewBox=\"0 0 24 24\"><path fill=\"currentColor\" d=\"M9 3l-1.5 2H5c-1.1 0-2 .9-2 2v11c0 1.1.9 2 2 2h14c1.1 0 2-.9 2-2V7c0-1.1-.9-2-2-2h-2.5L15 3H9zm3 5a5 5 0 1 1 0 10 5 5 0 0 1 0-10zm0 2a3 3 0 1 0 0 6 3 3 0 0 0 0-6z\"/></svg>";

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
    bool screenshotAnimating = false;
    // hover dropdown state
    VirtHost* hoverHost = nullptr;
    int hoverCmdId = 0;
    int hoverPendingCmdId = 0;
    VirtCtrl* hoverButton = nullptr;  // The button that triggered the hover
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

void HideFloatingToolbarHoverDropdown(FloatingToolbar* tb);
Rect GetFloatingToolbarButtonScreenRect(MainWindow* win, int cmdId);

struct FloatingIconButton : VirtIconButton {
    int sideLen = 0;
    Color hoverBg = kColorUnset;
    FloatingToolbar* toolbar = nullptr;
    Pixmap* pixmapActive = nullptr;
    bool wasHovered = false;  // Track previous hover state

    Size GetIdealSize() override {
        return {sideLen, sideLen};
    }

    void Paint(VirtPaintCtx& ctx) override {
        bool paletteOpen = toolbar && id == CmdCommandPalette && IsCommandPaletteOpen(toolbar->win);
        bool active = (toolbar && toolbar->activeCmdId == id) || paletteOpen;
        bool screenshotFlash = toolbar && toolbar->screenshotAnimating && id == CmdScreenshot;
        bool isHovered = IsEnabled() && HasFlag(vwfHovered);

        // Detect hover state changes for annotation color commands
        if (toolbar && toolbar->host && IsAnnotColorCmd(id)) {
            if (isHovered && !wasHovered) {
                // Mouse just entered this button
                if (toolbar->hoverPendingCmdId != id) {
                    // Kill any pending close timer when entering a new button
                    toolbar->host->KillTimer(kFloatingToolbarCloseHoverDropdownTimerId);

                    // If a different dropdown was open, close it immediately
                    if (toolbar->hoverCmdId != 0 && toolbar->hoverCmdId != id) {
                        HideFloatingToolbarHoverDropdown(toolbar);
                    }

                    toolbar->hoverPendingCmdId = id;
                    toolbar->hoverButton = this;  // Store reference to this button

                    // Kill any leftover open timer
                    toolbar->host->KillTimer(kFloatingToolbarOpenHoverDropdownTimerId);
                    // Start fresh timer for this button
                    toolbar->host->SetTimer(kFloatingToolbarOpenHoverDropdownTimerId, UiTooltipDelayMs());
                }
            } else if (!isHovered && wasHovered) {
                // Mouse just left this button
                if (toolbar->hoverPendingCmdId == id) {
                    toolbar->hoverPendingCmdId = 0;
                    toolbar->hoverButton = nullptr;
                    toolbar->host->KillTimer(kFloatingToolbarOpenHoverDropdownTimerId);
                }
                // If the hover dropdown is open for this button, set a timer to close it
                if (toolbar->hoverCmdId == id) {
                    toolbar->host->SetTimer(kFloatingToolbarCloseHoverDropdownTimerId, 150);
                }
            }
        }
        wasHovered = isHovered;

        // Paint hover first, then the selected state on top. This keeps the
        // hover feedback available without ever covering the selection state.
        if (isHovered && hoverBg != kColorUnset) {
            ctx.gfx->FillRoundedRect(ctx.bounds, DpiScale(6), hoverBg);
        }
        if (active || screenshotFlash) {
            ctx.gfx->FillRoundedRect(ctx.bounds, DpiScale(6), MkRgb(0x3e, 0x53, 0x68));
        }

        Pixmap* px = (active || screenshotFlash) && pixmapActive ? pixmapActive : pixmap;
        if (px) {
            Size s2 = {px->width, px->height};
            int x = ctx.content.x + (ctx.content.dx - s2.dx) / 2;
            int y = ctx.content.y + (ctx.content.dy - s2.dy) / 2;
            ctx.gfx->DrawPixmap(px, {x, y, s2.dx, s2.dy});
        }
    }
};

void HideFloatingToolbarHoverDropdown(FloatingToolbar* tb) {
    if (!tb) {
        return;
    }
    if (tb->hoverHost) {
        VirtHost* h = tb->hoverHost;
        tb->hoverHost = nullptr;
        delete h;
    }
    tb->hoverCmdId = 0;
    tb->hoverPendingCmdId = 0;
    tb->hoverButton = nullptr;
    if (tb->host) {
        tb->host->KillTimer(kFloatingToolbarOpenHoverDropdownTimerId);
        tb->host->KillTimer(kFloatingToolbarCloseHoverDropdownTimerId);
    }
}

static void OpenFloatingToolbarHoverDropdown(FloatingToolbar* tb, int cmdId, VirtCtrl* button) {
    if (!tb || !tb->win || !tb->host || cmdId == 0 || !button) {
        return;
    }

    // Create a temporary event to build the dropdown content
    ToolbarHoverBuildEvent ev;
    ev.win = tb->win;
    ev.layout = nullptr;
    ev.centerOnButton = false;

    // Build the color menu
    BuildAnnotColorsHoverMenuForCmd(tb->win, cmdId, &ev);

    if (!ev.layout) {
        return;
    }

    // Create a VirtHost for the dropdown
    VirtHost::CreateArgs args;
    args.parent = tb->win->hwndFrame;
    args.className = WStrL(L"SumatraFloatingToolbarHoverMenu");
    args.isPopup = true;
    args.visible = false;
    args.noActivate = true;
    args.userData = tb->win;
    args.bgColor = ThemeControlBackgroundColor();
    args.isRtl = IsUIRtl();
    args.initialSize = {100, 100};

    VirtHost* host = VirtHost::Create(args);
    if (!host) {
        delete ev.layout;
        return;
    }

    Size sz = host->SetLayoutSizedToContent(ev.layout);

    // Get the button's bounds in window (floating toolbar client area) coordinates
    Rect buttonInWindow = button->BoundsInWindow();

    // Get the floating toolbar's screen position
    Rect toolbarScreenRect = tb->host->ScreenRect();

    // Convert button bounds from toolbar-relative to screen coordinates
    Rect buttonScreenRect;
    buttonScreenRect.x = toolbarScreenRect.x + buttonInWindow.x;
    buttonScreenRect.y = toolbarScreenRect.y + buttonInWindow.y;
    buttonScreenRect.dx = buttonInWindow.dx;
    buttonScreenRect.dy = buttonInWindow.dy;

    // Get the frame rect to determine if button is on left or right half of window
    RECT frameRect{};
    GetWindowRect(tb->win->hwndFrame, &frameRect);
    int frameWidth = frameRect.right - frameRect.left;
    int buttonCenterX = buttonScreenRect.x + buttonScreenRect.dx / 2;
    int frameCenterX = frameRect.left + frameWidth / 2;

    // Decide positioning based on which half of the window the button is in
    int x;
    if (buttonCenterX < frameCenterX) {
        // Button is on left half, show dropdown to the right of the button
        x = buttonScreenRect.x + buttonScreenRect.dx + DpiScale(6);
    } else {
        // Button is on right half, show dropdown to the left of the button
        x = buttonScreenRect.x - sz.dx - DpiScale(6);
    }

    // Vertical position: centered alongside the button
    int y = buttonScreenRect.y + ((buttonScreenRect.dy - sz.dy) / 2);

    Rect r{x, y, sz.dx, sz.dy};
    r = ShiftRectToWorkArea(r, tb->win->hwndFrame, true);
    host->SetPos(r, true);

    tb->hoverHost = host;
    tb->hoverCmdId = cmdId;
    tb->hoverPendingCmdId = 0;
    tb->hoverButton = button;

    // Kill any pending timers
    if (tb->host) {
        tb->host->KillTimer(kFloatingToolbarOpenHoverDropdownTimerId);
        tb->host->KillTimer(kFloatingToolbarCloseHoverDropdownTimerId);
    }
}

static void OnFloatingButton(FloatingToolbar* tb, VirtMouseEvent* ev) {
    if (!tb || !ev || !ev->target) {
        return;
    }
    int cmd = ev->target->id;
    if (!cmd) {
        return;
    }

    // Hide hover dropdown when a button is clicked
    HideFloatingToolbarHoverDropdown(tb);

    if (cmd == CmdCommandPalette) {
        HwndPostCommand(tb->win->hwndFrame, cmd, 0);
        return;
    }

    if (cmd == CmdScreenshot) {
        TempStr savedPath = TakeScreenshotOfWindow(tb->win->hwndCanvas);
        if (len(savedPath) > 0) {
            str::Builder msg;
            msg.Append(fmt("Saved screenshot to '%s'", savedPath));
            NotificationCreateArgs args;
            args.hwndParent = tb->win->hwndCanvas;
            args.font = GetDefaultGuiFont();
            args.timeoutMs = kNotifDefaultTimeOut;
            args.msg = ToStr(msg);
            args.plainText = true;
            ShowNotification(args);
        }
        tb->screenshotAnimating = true;
        SetTimer(tb->host->native, 1, 300, nullptr);
        tb->host->Invalidate(false);
        return;
    }

    bool isAnnotTool = (cmd == CmdAnnotationHighlightBrush || cmd == CmdCreateAnnotInk || cmd == CmdCreateAnnotFreeText ||
                        cmd == CmdCreateAnnotUnderline || cmd == CmdCreateAnnotSquiggly || cmd == CmdCreateAnnotStrikeOut ||
                        (cmd >= CmdCreateAnnotFirst && cmd <= CmdCreateAnnotLast));

    // If Edit PDF temporarily revealed the normal top toolbar, any non-annotation action restores its hidden state.
    if (cmd != CmdToggleEditPDF && !isAnnotTool && tb->win->floatingEditPdfRevealedToolbar) {
        tb->win->floatingEditPdfRevealedToolbar = false;
        tb->win->isToolbarVisible = false;
        if (tb->win->hwndToolbar) {
            ShowWindow(tb->win->hwndToolbar, SW_HIDE);
        }
        ScheduleUiUpdate(tb->win, kUiForceRelayout | kUiRelayout);
    }

    // Clicking the already active tool again deactivates it
    if (tb->activeCmdId == cmd) {
        CancelAnnotationPlacement(tb->win);
        if (cmd == CmdToggleEditPDF) {
            if (tb->win->floatingEditPdfRevealedToolbar) {
                tb->win->floatingEditPdfRevealedToolbar = false;
                tb->win->isToolbarVisible = false;
                if (tb->win->hwndToolbar) {
                    ShowWindow(tb->win->hwndToolbar, SW_HIDE);
                }
                ScheduleUiUpdate(tb->win, kUiForceRelayout | kUiRelayout);
            }
            HwndPostCommand(tb->win->hwndFrame, cmd, 0);
            // the toggle is posted, not sent: clear the highlight now so the
            // button is not lit while the command is still in the queue
            tb->activeCmdId = 0;
        } else if (!tb->win->isToolbarVisible && !tb->win->isToolbarOverlay && tb->win->pdfAnnotationsToolbarEnabled) {
            // toolbar is hidden: deselecting the tool also exits Edit PDF
            // mode, so no half-on state is left behind. The toggle is posted,
            // not sent, so clear the highlight now; the command re-syncs
            // once it lands
            HwndPostCommand(tb->win->hwndFrame, CmdToggleEditPDF, 0);
            tb->activeCmdId = 0;
        } else {
            // the placement tool ended but Edit PDF mode stays on: re-sync so
            // the toolbar highlights Edit PDF instead of ending up with no
            // active button
            UpdateFloatingToolbarActiveState(tb->win);
        }
        tb->host->Invalidate(false);
        return;
    }

    // Switching to a new tool: cancel old placement and activate new tool instantly in 1 click
    bool wasPlacing = IsPlacingAnnotation(tb->win);
    bool wasPdfEditEnabled = tb->win->pdfAnnotationsToolbarEnabled;
    CancelAnnotationPlacement(tb->win);
    tb->activeCmdId = cmd;
    tb->host->Invalidate(false);

    if (cmd == CmdToggleEditPDF) {
        if (!tb->win->isToolbarVisible && !tb->win->isToolbarOverlay && tb->win->hwndToolbar) {
            tb->win->floatingEditPdfRevealedToolbar = true;
            tb->win->isToolbarVisible = true;
            ShowWindow(tb->win->hwndToolbar, SW_SHOW);
            ScheduleUiUpdate(tb->win, kUiForceRelayout | kUiRelayout);
        }
        if (wasPlacing || wasPdfEditEnabled) {
            EnablePdfAnnotationsToolbar(tb->win);
            ToolbarUpdateStateForWindow(tb->win, true);
        } else {
            HwndPostCommand(tb->win->hwndFrame, cmd, 0);
        }
        return;
    }

    if (isAnnotTool) {
        // picking an annotation tool from floating toolbar will not show system edit toolbar
        if (tb->win->floatingEditPdfRevealedToolbar) {
            tb->win->floatingEditPdfRevealedToolbar = false;
            tb->win->isToolbarVisible = false;
            if (tb->win->hwndToolbar) {
                ShowWindow(tb->win->hwndToolbar, SW_HIDE);
            }
            ScheduleUiUpdate(tb->win, kUiForceRelayout | kUiRelayout);
        }
        ToolbarUpdateStateForWindow(tb->win, false);
        HwndSendCommand(tb->win->hwndFrame, cmd, 0);

        if (IsAnnotColorCmd(cmd)) {
            tb->hoverPendingCmdId = cmd;
            tb->hoverButton = ev->target;
            tb->host->KillTimer(kFloatingToolbarOpenHoverDropdownTimerId);
            tb->host->SetTimer(kFloatingToolbarOpenHoverDropdownTimerId, UiTooltipDelayMs());
        }

        return;
    }

    HwndPostCommand(tb->win->hwndFrame, cmd, 0);
}

static void PaintFloatingToolbar(FloatingToolbar*, VirtHostPaintEvent* ev) {
    ev->gfx->FillRoundedRect(ev->clientRect, DpiScale(kFloatingToolbarRadius), FloatingBg(), FloatingBorder());
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

static int GetFloatingToolbarMinY(MainWindow* win, int frameTop) {
    int topConstraint = frameTop;
    if (win->hwndToolbar && IsWindowVisible(win->hwndToolbar)) {
        RECT tr{};
        GetWindowRect(win->hwndToolbar, &tr);
        POINT pt{tr.left, tr.bottom};
        ScreenToClient(win->hwndFrame, &pt);
        topConstraint = frameTop + pt.y;
    } else if (!win->captionRect.IsEmpty()) {
        topConstraint = frameTop + win->captionRect.y + win->captionRect.dy;
    }
    return topConstraint + DpiScale(10);
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
                     ((int)dimof(gButtons) - 1) * kFloatingToolbarGap +
                     2 * kFloatingToolbarSeparatorGap +
                     kFloatingToolbarButtonSize);

    if (!FloatingToolbarIsForPdf(tb)) {
        ShowWindow(tb->host->native, SW_HIDE);
        return;
    }
    ShowWindow(tb->host->native, SW_SHOWNOACTIVATE);

    int minY = GetFloatingToolbarMinY(tb->win, fr.y);
    int x = fr.x + DpiScale(12);
    int y = minY;

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
    // Clamp after applying the sidebar position too. A wide sidebar or a
    // restored position must never allow the popup outside the frame.
    int minX = fr.x + DpiScale(12);
    int maxX = std::max(minX, fr.x + fr.dx - w - DpiScale(12));
    int maxY = std::max(minY, fr.y + fr.dy - h - DpiScale(12));
    x = std::clamp(x, minX, maxX);
    y = std::clamp(y, minY, maxY);
    MoveFloatingToolbar(tb, {x, y, w, h});

    // Persist the position relative to the frame. Positive X stores the
    // left offset; negative X stores the right offset. Y always stores the
    // offset from the top edge. This is the canonical saved position used
    // after the next launch.
    if (gSettings) {
        int frameCenter = fr.x + fr.dx / 2;
        int toolbarCenter = x + w / 2;
        if (toolbarCenter <= frameCenter) {
            gSettings->floatingToolbarPosition.x = x - fr.x;
        } else {
            int rightOffset = fr.x + fr.dx - (x + w);
            gSettings->floatingToolbarPosition.x = -rightOffset;
        }
        gSettings->floatingToolbarPosition.y = y - fr.y;
        if (frameWasResized) {
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
    case WM_TIMER:
        if (ev->wp == 1) {
            KillTimer(tb->host->native, 1);
            tb->screenshotAnimating = false;
            tb->host->Invalidate(false);
            ev->didHandle = true;
        } else if (ev->wp == kFloatingToolbarOpenHoverDropdownTimerId) {
            tb->host->KillTimer(kFloatingToolbarOpenHoverDropdownTimerId);
            int cmdId = tb->hoverPendingCmdId;
            VirtCtrl* button = tb->hoverButton;
            tb->hoverPendingCmdId = 0;
            if (cmdId != 0 && button) {
                OpenFloatingToolbarHoverDropdown(tb, cmdId, button);
            }
            ev->didHandle = true;
        } else if (ev->wp == kFloatingToolbarCloseHoverDropdownTimerId) {
            tb->host->KillTimer(kFloatingToolbarCloseHoverDropdownTimerId);
            Point ptScreen = UiCursorScreenPos();
            bool overPopup = tb->hoverHost && tb->hoverHost->ScreenRect().Contains(ptScreen);
            bool overButton = false;
            if (tb->hoverButton && tb->hoverCmdId != 0) {
                Rect bRect = GetFloatingToolbarButtonScreenRect(tb->win, tb->hoverCmdId);
                overButton = bRect.Contains(ptScreen);
            }
            if (overPopup || overButton) {
                // Mouse is still on button or in popup window: keep open and check again soon
                tb->host->SetTimer(kFloatingToolbarCloseHoverDropdownTimerId, 150);
            } else if (tb->hoverPendingCmdId == 0) {
                HideFloatingToolbarHoverDropdown(tb);
            }
            ev->didHandle = true;
        }
        break;
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
            int minX = frame.left + DpiScale(12);
            int minY = GetFloatingToolbarMinY(tb->win, frame.top);
            int maxX = std::max<int>(minX, frame.right - tb->dragOrig.dx - DpiScale(12));
            int maxY = std::max<int>(minY, frame.bottom - tb->dragOrig.dy - DpiScale(12));
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
                RECT frame{};
                GetWindowRect(tb->win->hwndFrame, &frame);
                Rect r = tb->host->ScreenRect();
                int frameCenter = frame.left + (frame.right - frame.left) / 2;
                int toolbarCenter = r.x + r.dx / 2;
                if (toolbarCenter <= frameCenter) {
                    gSettings->floatingToolbarPosition.x = r.x - frame.left;
                } else {
                    gSettings->floatingToolbarPosition.x =
                        -(frame.right - (r.x + r.dx));
                }
                gSettings->floatingToolbarPosition.y = r.y - frame.top;
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

static void PaintFloatingToolbarSeparator(VirtPaintCtx* ctx) {
    if (!ctx) {
        return;
    }
    Rect r = ctx->bounds;
    int y = r.y + r.dy / 2;
    ctx->gfx->DrawLine({r.x, y, r.dx, 1}, FloatingBorder(), DpiScale(1));
}

static VirtCtrl* MakeFloatingToolbarSeparator(int width) {
    auto* sep = new VirtCustom();
    sep->idealSize = {width, DpiScale(1)};
    sep->onPaint = MkFunc1Void(PaintFloatingToolbarSeparator);
    sep->SetFlag(vwfNoHitTest, true);
    return sep;
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
        button->pixmapActive = GetCachedPixmapForSvg(Str(b.icon), iconSize, iconSize,
                                                kColWhite, MkRgb(0x3e, 0x53, 0x68));
        button->SetTooltip(Str(b.tip));
        button->id = b.cmdId;
        button->onClick = MkFunc1(OnFloatingButton, tb);
        box->AddChild(button);
        if (i + 1 != (int)dimof(gButtons)) {
            box->AddChild(new Spacer(gap, gap));
        }
    }

    box->AddChild(new Spacer(DpiScale(kFloatingToolbarSeparatorGap), DpiScale(kFloatingToolbarSeparatorGap)));
    box->AddChild(MakeFloatingToolbarSeparator(buttonSize));
    box->AddChild(new Spacer(DpiScale(kFloatingToolbarSeparatorGap), DpiScale(kFloatingToolbarSeparatorGap)));

    auto* screenshot = new FloatingIconButton();
    screenshot->sideLen = buttonSize;
    screenshot->hoverBg = FloatingHover();
    screenshot->toolbar = tb;
    screenshot->pixmap = GetCachedPixmapForSvg(Str(kScreenshotIcon), iconSize, iconSize,
                                                ThemeWindowTextColor(), FloatingBg());
    screenshot->pixmapActive = GetCachedPixmapForSvg(Str(kScreenshotIcon), iconSize, iconSize,
                                                kColWhite, MkRgb(0x3e, 0x53, 0x68));
    screenshot->SetTooltip(StrL("Screenshot"));
    screenshot->id = CmdScreenshot;
    screenshot->onClick = MkFunc1(OnFloatingButton, tb);
    box->AddChild(screenshot);

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
    // Hide hover dropdown before destroying toolbar
    HideFloatingToolbarHoverDropdown(tb);
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

void UpdateFloatingToolbarActiveState(MainWindow* win) {
    if (!win || !win->floatingToolbar) {
        return;
    }
    FloatingToolbar* tb = win->floatingToolbar;
    int activeCmd = 0;
    if (IsPlacingAnnotation(win)) {
        activeCmd = win->annotPlacement.cmdId;
    } else if (win->pdfAnnotationsToolbarEnabled) {
        activeCmd = CmdToggleEditPDF;
    }
    tb->activeCmdId = activeCmd;
    if (tb->host) {
        tb->host->Invalidate(false);
    }
}

bool IsCursorOverFloatingToolbar(MainWindow* win) {
    if (!win || !win->floatingToolbar || !win->floatingToolbar->host) {
        return false;
    }
    POINT ptScreen{};
    GetCursorPos(&ptScreen);
    HWND hwndUnderCursor = WindowFromPoint(ptScreen);
    HWND floatingHwnd = win->floatingToolbar->host->native;
    return hwndUnderCursor == floatingHwnd || IsChild(floatingHwnd, hwndUnderCursor);
}

static VirtCtrl* FindButtonInLayout(ILayout* layout, int cmdId) {
    if (!layout) {
        return nullptr;
    }
    if (VirtCtrl* vc = layout->AsVirtCtrl()) {
        if (vc->id == cmdId) {
            return vc;
        }
    }
    int count = layout->LayoutChildCount();
    for (int i = 0; i < count; i++) {
        if (VirtCtrl* found = FindButtonInLayout(layout->LayoutChildAt(i), cmdId)) {
            return found;
        }
    }
    return nullptr;
}

Rect GetFloatingToolbarButtonScreenRect(MainWindow* win, int cmdId) {
    if (!win || !win->floatingToolbar || !win->floatingToolbar->host) {
        return {};
    }
    FloatingToolbar* tb = win->floatingToolbar;
    VirtHost* host = tb->host;
    if (!host || !host->vroot) {
        return {};
    }
    VirtCtrl* btn = FindButtonInLayout(host->vroot->owned, cmdId);
    if (!btn) {
        return {};
    }
    return host->ToScreen(btn->lastBounds);
}
