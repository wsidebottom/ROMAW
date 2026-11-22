/*+===================================================================
  File:      syserr_procedures_test.c

  Summary:   Reports native system-error procedure drawing and action regressions.

  Classes:   None.

  Functions: main.

  Origin:    Focused harness for diagnostics/syserr_procedures.c and its extracted
             DSAT procedure contracts, using synthetic guest memory and services.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int16_t INTEGER;
typedef int32_t LONGINT;
typedef uint32_t uint32;
typedef uint32_t syn68k_addr_t;
typedef unsigned char BOOLEAN;
typedef char* Ptr;

typedef struct {
    INTEGER v, h;
} Point;

typedef struct {
    INTEGER top, left, bottom, right;
} Rect;

#define PACKED __attribute__((packed))

static uint16_t swap_word(uint16_t value)
{
    return (value << 8) | (value >> 8);
}

static uint32_t swap_long(uint32_t value)
{
    return __builtin_bswap32(value);
}

#define CW(value) ((INTEGER)swap_word(value))
#define CL(value) swap_long(value)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
static unsigned char ram[8192];
uint32 ROMlib_guest_memory_size = sizeof(ram);
static uint32 EM_A5, EM_A7, EM_D0, ResumeProc;
static INTEGER DSErrCode, CurMap;
static Rect DSAlertRect;
#define CurApName (*(unsigned char(*)[34])(ram + 0x910))
#define SYN68K_TO_US(address) (ram + (address))
#define US_TO_SYN68K(pointer) ((uintptr_t)(pointer))
#define MR(value) ((void*)(uintptr_t)(value))
static unsigned callbacks, exits, restarts, shutdowns, warnings;
#define CALL_EMULATOR(address) (++callbacks, (void)(address))
#define warning_unimplemented(...) (++warnings)
static char drawn[2048];
static size_t drawn_length;
static INTEGER move_h, move_v;

static void C_MoveTo(INTEGER h, INTEGER v)
{
    move_h = h;
    move_v = v;
}

static void C_DrawText(Ptr text, INTEGER first, INTEGER length)
{
    if (length < 0 || drawn_length + length >= sizeof(drawn))
        abort();
    memcpy(drawn + drawn_length, text + first, length);
    drawn_length += length;
    drawn[drawn_length] = 0;
}

static void C_DrawChar(INTEGER value)
{
    char c = value;
    C_DrawText(&c, 0, 1);
}

static void C_UnionRect(const Rect* a, const Rect* b, Rect* result)
{
    Rect value = {CW(MIN(CW(a->top), CW(b->top))), CW(MIN(CW(a->left), CW(b->left))),
                  CW(-MIN(-CW(a->bottom), -CW(b->bottom))), CW(-MIN(-CW(a->right), -CW(b->right)))};
    *result = value;
}

static void C_ExitToShell(void)
{
    ++exits;
}

static void C_ShutDwnStart(void)
{
    ++restarts;
}

static void C_ShutDwnPower(void)
{
    ++shutdowns;
}

#include "../../diagnostics/syserr_procedures.c"

static unsigned checks;
#define CHECK(condition)                                                 \
    do {                                                                 \
        ++checks;                                                        \
        if (!(condition)) {                                              \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
            return 1;                                                    \
        }                                                                \
    } while (0)

static void clear_drawing(void)
{
    drawn_length = 0;
    drawn[0] = 0;
}

static void store_long(uint32 address, uint32 value)
{
    value = CL(value);
    memcpy(ram + address, &value, 4);
}

static void store_string(uint32 address, const char* text)
{
    size_t n = strlen(text);
    ram[address] = n;
    memcpy(ram + address + 1, text, n);
}

int main(void)
{
    ROMlib_syserr_context context = {0};
    context.offsetx = 3;
    context.offsety = 5;
    DSErrCode = CW(1);
    ROMlib_syserr_procedure(-21103, &context);
    CHECK(!strcmp(drawn, "bus error"));
    CHECK(move_h == 133 && move_v == 127);
    clear_drawing();
    DSErrCode = CW(7);
    ROMlib_syserr_procedure(-21106, &context);
    CHECK(!strcmp(drawn, "error type 7"));
    clear_drawing();
    DSErrCode = CW(99);
    ROMlib_syserr_procedure(-21103, &context);
    CHECK(!strcmp(drawn, "error type 99"));
    clear_drawing();
    DSErrCode = CW(100);
    ROMlib_syserr_procedure(-21103, &context);
    CHECK(!drawn_length);
    context.table = 2;
    store_string(0x910, "Demo");
    clear_drawing();
    ROMlib_syserr_procedure(-21108, &context);
    CHECK(!strcmp(drawn, "\322Demo\323"));
    CHECK(move_h == 117 && move_v == 113);
    clear_drawing();
    CurApName[0] = 0xff;
    store_long(0x914, 0xfffffffe);
    ROMlib_syserr_procedure(-21108, &context);
    CHECK(strstr(drawn, "hold down the shift key.") != NULL);
    CHECK(strstr(drawn, "Demo") == NULL);
    store_string(0x910, "Demo");
    clear_drawing();
    DSAlertRect = (Rect){CW(1), CW(2), CW(100), CW(200)};
    context.bounds = (Rect){CW(3), CW(4), CW(50), CW(80)};
    ROMlib_syserr_procedure(-21107, &context);
    CHECK(!strcmp(drawn, "Force \322Demo\323 to quit?"));
    CHECK(CW(context.bounds.bottom) == 100 && CW(context.bounds.right) == 200);
    ROMlib_syserr_procedure(-21112, &context);
    CHECK(CW(context.bounds.top) == 1);
    context.table = 0;
    ROMlib_syserr_procedure(-21000, &context);
    CHECK(exits == 1 && restarts == 0);
    context.table = 2;
    ROMlib_syserr_procedure(-21000, &context);
    CHECK(restarts == 1);
    ROMlib_syserr_procedure(-21105, &context);
    CHECK(shutdowns == 1);
    context.guest_a5 = 0x1234;
    context.guest_a7 = 0x5678;
    context.resource_map = CW(42);
    ROMlib_syserr_procedure(-21104, &context);
    CHECK(callbacks == 0);
    ResumeProc = 0x1000;
    ROMlib_syserr_procedure(-21104, &context);
    CHECK(callbacks == 1 && EM_A5 == 0x1234 && EM_A7 == 0x5678 && CW(CurMap) == 42);
    ram[0xbff] = 0xff;
    ram[0x120] = 0xff;
    ROMlib_syserr_procedure(-21102, &context);
    CHECK(ram[0x120] == 0x7f && exits == 2);
    store_long(0xbb8, 0x1000);
    store_long(0x1048, 0x1100);
    store_string(0x1100, "Disk");
    clear_drawing();
    ROMlib_syserr_procedure(-21101, &context);
    CHECK(!strcmp(drawn, "Please insert the disk:\322Disk\323"));
    CHECK(move_h == 75 && move_v == 45);
    clear_drawing();
    ROMlib_syserr_procedure(-21110, &context);
    CHECK(strstr(drawn, "Please insert the disk:This disk contains files orprograms that are in use.") == drawn);
    store_long(0x1102, 0x1200);
    store_string(0x1200, "Volume");
    clear_drawing();
    ROMlib_syserr_procedure(-21109, &context);
    CHECK(!strcmp(drawn, "Please eject the disk:\322Volume\323Press the eject button."));
    CHECK(move_h == 75 && move_v == 61);
    clear_drawing();
    EM_D0 = 0x1200;
    ROMlib_syserr_procedure(-21111, &context);
    CHECK(!strcmp(drawn, "\322Volume\323"));
    ROMlib_syserr_procedure(-1, &context);
    CHECK(warnings == 1);
    printf("PASS: %u system-error procedure checks across all 13 native identities\n", checks);
    return 0;
}
