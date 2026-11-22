/*+===================================================================
  File:      menu_list_probe.c

  Summary:   A cleared private MenuList is used by ResEdit's DITL editor.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* A cleared private MenuList is used by ResEdit's DITL editor. */
#include <Multiverse.h>
#include <stdio.h>

int main(void)
{
    Handle saved, empty;
    MenuHandle first, second, visible;
    short report;
    long length;
    int failures = 0;
    char text[80];
    Str255 path = "\pD:Source:ROMAW:build:menu-list-probe.txt";
    HCreate(0, 0, path, 'MnPr', 'TEXT');
    if (HOpen(0, 0, path, fsRdWrPerm, &report))
        return 1;
    SetEOF(report, 0);
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    saved = GetMenuBar();
    empty = NewHandleClear(256);
    SetMenuBar(empty);
    DisposeHandle(empty);
    DeleteMenu(3105); /* Deletion can be the first operation on a cleared list. */
    if (GetMenuHandle(3105))
        ++failures;
    first = NewMenu(3105, "\pFirst");
    second = NewMenu(3106, "\pSecond");
    visible = NewMenu(3107, "\pVisible");
    InsertMenu(first, -1);
    InsertMenu(second, -1);
    InsertMenu(first, -1); /* No duplicate hierarchical entry. */
    InsertMenu(visible, 0);
    if (GetMenuHandle(3105) != first || GetMenuHandle(3106) != second || GetMenuHandle(3107) != visible)
        ++failures;
    DeleteMenu(3105);
    if (GetMenuHandle(3105) || GetMenuHandle(3106) != second)
        ++failures;
    DeleteMenu(3106);
    DeleteMenu(3107);
    InsertMenu(second, -1);
    if (GetMenuHandle(3106) != second)
        ++failures;
    empty = NewHandleClear(256);
    SetMenuBar(empty);
    DisposeHandle(empty);
    if (GetMenuHandle(3105)) /* Lookup can also precede the first insertion. */
        ++failures;
    InsertMenu(visible, 0);
    InsertMenu(second, -1);
    if (GetMenuHandle(3107) != visible || GetMenuHandle(3106) != second)
        ++failures;
    SetMenuBar(saved);
    DisposeHandle(saved);
    DisposeMenu(first);
    DisposeMenu(second);
    DisposeMenu(visible);
    length = sprintf(text, "cleared private menu list failures=%d\n", failures);
    FSWrite(report, &length, text);
    FSClose(report);
    return failures != 0;
}
