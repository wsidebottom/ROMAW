/*+===================================================================
  File:      classic_ui_test.c

  Summary:   The same renderer builds as plain C on Linux and Windows; its operation stream must retain the original
             WDEF/MBDF coordinates and colour roles.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* The same renderer builds as plain C on Linux and Windows; its operation
   stream must retain the original WDEF/MBDF coordinates and colour roles. */
#include "classic-ui/classic_ui.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct Operation {
    int kind;
    ROMAWClassicRect bounds;
    ROMAWClassicColor role;
} Operation;

typedef struct Recording {
    Operation operations[32];
    unsigned count;
} Recording;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            fprintf(stderr, "Classic UI check failed at line %d: %s\n", __LINE__, #condition); \
            exit(1);                                                                           \
        }                                                                                      \
    } while (0)

static void record(Recording* recording, int kind, ROMAWClassicRect bounds, ROMAWClassicColor role)
{
    CHECK(recording->count < 32);
    Operation* op = &recording->operations[recording->count++];
    op->kind = kind;
    op->bounds = bounds;
    op->role = role;
}

static void fill_rect(void* context, const ROMAWClassicRect* bounds, ROMAWClassicColor role)
{
    record(context, 1, *bounds, role);
}

static void stroke_line(void* context, int x, int y, int end_x, int end_y, ROMAWClassicColor role)
{
    ROMAWClassicRect endpoints = {x, y, end_x, end_y};
    record(context, 2, endpoints, role);
}

static void frame_rect(void* context, const ROMAWClassicRect* bounds, ROMAWClassicColor role)
{
    record(context, 3, *bounds, role);
}

static int is_rect(ROMAWClassicRect r, int left, int top, int right, int bottom)
{
    return r.left == left && r.top == top && r.right == right && r.bottom == bottom;
}

int main(void)
{
    CHECK(romaw_classic_arrow_state(0, 0) == ROMAW_CLASSIC_ARROW_INACTIVE);
    CHECK(romaw_classic_arrow_state(0, 1) == ROMAW_CLASSIC_ARROW_INACTIVE);
    CHECK(romaw_classic_arrow_state(1, 0) == ROMAW_CLASSIC_ARROW_ACTIVE);
    CHECK(romaw_classic_arrow_state(1, 1) == ROMAW_CLASSIC_ARROW_CLICKED);
    Recording recording = {0};
    ROMAWClassicCanvas canvas = {&recording, fill_rect, stroke_line, frame_rect};
    ROMAWClassicRect content = {100, 120, 400, 500};
    romaw_classic_document_frame(&canvas, &content, 19);
    CHECK(recording.count == 5);
    CHECK(recording.operations[0].kind == 1);
    CHECK(is_rect(recording.operations[0].bounds, 99, 101, 401, 120));
    CHECK(recording.operations[0].role == ROMAW_CLASSIC_TITLE_BACKGROUND);
    CHECK(recording.operations[1].kind == 3);
    CHECK(is_rect(recording.operations[1].bounds, 99, 101, 401, 501));
    CHECK(recording.operations[1].role == ROMAW_CLASSIC_FRAME_OUTLINE);
    CHECK(is_rect(recording.operations[2].bounds, 100, 119, 400, 119));
    CHECK(is_rect(recording.operations[3].bounds, 100, 501, 401, 501));
    CHECK(is_rect(recording.operations[4].bounds, 401, 102, 401, 500));
    CHECK(recording.operations[4].role == ROMAW_CLASSIC_FRAME_SHADOW);

    recording.count = 0;
    romaw_classic_title_bevel(&canvas, 100, 120, 400, 19);
    romaw_classic_title_rules(&canvas, 100, 120, 400, 19);
    CHECK(recording.count == 10);
    CHECK(is_rect(recording.operations[0].bounds, 100, 118, 399, 118));
    CHECK(recording.operations[0].role == ROMAW_CLASSIC_TITLE_SHADOW);
    CHECK(is_rect(recording.operations[2].bounds, 398, 102, 100, 102));
    CHECK(recording.operations[2].role == ROMAW_CLASSIC_TITLE_LIGHT);
    CHECK(is_rect(recording.operations[4].bounds, 101, 105, 398, 105));
    CHECK(is_rect(recording.operations[9].bounds, 101, 115, 398, 115));
    CHECK(recording.operations[9].role == ROMAW_CLASSIC_TITLE_RULE);
    recording.count = 0;
    romaw_classic_title_rules(&canvas, 100, 120, 400, 14);
    CHECK(recording.count == 3);

    recording.count = 0;
    ROMAWClassicRect bar = {10, 0, 50, 20};
    romaw_classic_menu_title(&canvas, &bar, 1);
    CHECK(recording.count == 3);
    CHECK(is_rect(recording.operations[0].bounds, 10, 0, 50, 19));
    CHECK(recording.operations[0].role == ROMAW_CLASSIC_MENU_BACKGROUND);
    CHECK(is_rect(recording.operations[1].bounds, 10, 19, 49, 19));
    CHECK(recording.operations[1].role == ROMAW_CLASSIC_MENU_OUTLINE);
    CHECK(is_rect(recording.operations[2].bounds, 10, 1, 50, 19));
    CHECK(recording.operations[2].role == ROMAW_CLASSIC_MENU_HIGHLIGHT);
    recording.count = 0;
    romaw_classic_menu_title(&canvas, &bar, 0);
    CHECK(recording.count == 2);
    CHECK(romaw_classic_menu_advance(23) == 36);
    CHECK(romaw_classic_menu_advance(-1) == 13);
    CHECK(romaw_classic_menu_advance(INT_MAX) == INT_MAX);

    recording.count = 0;
    canvas.frame_rect = NULL;
    romaw_classic_document_frame(&canvas, &content, 19);
    CHECK(recording.count == 8);
    recording.count = 0;
    romaw_classic_document_frame(NULL, &content, 19);
    romaw_classic_title_rules(&canvas, 0, 0, 0, 0);
    ROMAWClassicRect empty = {0, 0, 0, 0};
    romaw_classic_menu_title(&canvas, &empty, 1);
    CHECK(recording.count == 0);
    puts("PASS: shared WDEF/MBDF geometry, roles, selection and plain C portability");
    return 0;
}
