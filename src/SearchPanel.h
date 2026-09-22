/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

struct MainWindow;

void CreateSearchPanel(MainWindow* win);
void DestroySearchPanel(MainWindow* win);
void OpenSearchSelectionInSidebar(MainWindow* win, Str engineName, Str url);
void RelayoutSearchPanel(MainWindow* win);
