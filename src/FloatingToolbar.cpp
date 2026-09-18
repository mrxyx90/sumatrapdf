static void OnFloatingButton(FloatingToolbar* tb, VirtMouseEvent* ev) {
    if (!tb || !ev || !ev->target) {
        return;
    }
    int cmd = ev->target->id;
    if (!cmd) {
        return;
    }

    // If Edit PDF temporarily revealed the normal top toolbar, any other
    // floating-toolbar action should restore its previously hidden state.
    if (cmd != CmdToggleEditPDF && tb->win->floatingEditPdfRevealedToolbar) {
        tb->win->floatingEditPdfRevealedToolbar = false;
        tb->win->isToolbarVisible = false;
        if (tb->win->hwndToolbar) {
            ShowWindow(tb->win->hwndToolbar, SW_HIDE);
        }
        ScheduleUiUpdate(tb->win, kUiForceRelayout | kUiRelayout);
    }

    if (cmd == CmdScreenshot) {
        // Screenshot is independent from the annotation tools: keep the
        // current tool selection and just invoke the screenshot command.
        TakeScreenshotOfWindow(tb->win->hwndCanvas);
        return;
    }

    // Edit PDF can temporarily reveal the normal top toolbar when the user
    // has it hidden. Only hide it again if this click was what revealed it.
    if (cmd == CmdToggleEditPDF) {
        if (tb->activeCmdId == cmd) {
            if (tb->win->floatingEditPdfRevealedToolbar) {
                tb->win->floatingEditPdfRevealedToolbar = false;
                tb->win->isToolbarVisible = false;
                if (tb->win->hwndToolbar) {
                    ShowWindow(tb->win->hwndToolbar, SW_HIDE);
                }
                ScheduleUiUpdate(tb->win, kUiForceRelayout | kUiRelayout);
            }
            HwndPostCommand(tb->win->hwndFrame, cmd, 0);
            tb->activeCmdId = 0;
            tb->host->Invalidate(false);
            return;
        }

        if (!tb->win->isToolbarVisible && !tb->win->isToolbarOverlay && tb->win->hwndToolbar) {
            tb->win->floatingEditPdfRevealedToolbar = true;
            tb->win->isToolbarVisible = true;
            ShowWindow(tb->win->hwndToolbar, SW_SHOW);
            ScheduleUiUpdate(tb->win, kUiForceRelayout | kUiRelayout);
        }
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

static bool FloatingToolbarIsForPdf(FloatingToolbar* tb) {
    return tb && tb->win && tb->win->IsDocLoaded() && tb->win->AsFixed() != nullptr;
}

static void MoveFloatingToolbar(FloatingToolbar* tb, Rect r) {
    if (!tb || !tb->host) {
        return;
    }
    tb->lastToolbarRect = r;
    // Keep this popup above the document and sidebar child windows. The