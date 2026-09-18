void MainWindow::UpdateCanvasSize() {
    if (suppressCanvasSizeUpdate) {
        FloatingToolbarRelayout(this);
        return;
    }
    // The bookmark sidebar can change width even when the canvas rect does
    // not change. Relayout the floating toolbar before the early return so it
    // stays attached to the sidebar border.
    FloatingToolbarRelayout(this);

    Rect rc = HwndClientRect(hwndCanvas);
    if (buffer && canvasRc == rc) {
        return;
    }
    canvasRc = rc;

    // create a new output buffer and notify the model
    // about the change of the canvas size
    delete buffer;
    buffer = new DoubleBuffer(hwndCanvas, canvasRc);

    if (IsDocLoaded()) {
        // the display model needs to know the full size (including scroll bars)
        ctrl->SetViewPortSize(GetViewPortSize());
    }
    if (CurrentTab()) {
        CurrentTab()->canvasRc = canvasRc;
    }

    // The bookmarks sidebar changes the canvas area without moving the frame.