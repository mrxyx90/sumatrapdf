/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

struct MainWindow;

void FloatingToolbarCreate(MainWindow*);
void FloatingToolbarDestroy(MainWindow*);
void FloatingToolbarOnWindowMoved(MainWindow*);
void FloatingToolbarRelayout(MainWindow*);
void FloatingToolbarUpdateTheme();
void UpdateFloatingToolbarActiveState(MainWindow*);
bool IsCursorOverFloatingToolbar(MainWindow*);
