/*+===================================================================
  File:      movie_runtime_test.c

  Summary:   Real Movie Toolbox lifetime/timeline/decoder with intercepted file/graphics.

  Classes:   None.

  Functions: CToPascalCall, C_CopyBits, C_DisposeGWorld, C_FSpOpenDF, C_GetGWorld, C_GetGWorldPixMap, C_GetPixBaseAddr,
             C_LockPixels, C_NewGWorld, C_SetGWorld, C_TickCount, C_UnlockPixels, DisposPtr, FSClose, FSRead, GetEOF,
             GetFPos, SetFPos, _NewPtr_flags, main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Real Movie Toolbox lifetime/timeline/decoder with intercepted file/graphics. */
#define NDEBUG 1
#include "../../movies/qt.c"
#undef main
#undef NDEBUG
#include <assert.h>
#include "movie_fixture.h"

#if SIZEOF_CHAR_P == 8
uint64 ROMlib_offset;
#else
uint32 ROMlib_offset;
#endif
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base = 1, syn68k_host_alias_size = 128;
void* tooltraptable[0x400];
toolstuff_t toolstuff[0x400];
static unsigned now, mark = 17, live, draws, disposed, fail_read, fail_token;
static CGrafPort target, world;
static PixMap pixels;
static PixMapPtr pixels_pointer;
static uint8 output[16];
static ptr_sized_uint pointer_map[128];
static unsigned pointer_count;

void* syn68k_guest_to_host(uint32 address)
{
    assert(address && address <= pointer_count);
    return (void*)pointer_map[address - 1];
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    for (unsigned i = 0; i < pointer_count; ++i)
        if (pointer_map[i] == address)
            return i + 1;
    assert(pointer_count < sizeof pointer_map / sizeof pointer_map[0]);
    pointer_map[pointer_count++] = address;
    return pointer_count;
}

intptr_t CToPascalCall(void* function, unsigned long magic, ...)
{
    abort();
}

Ptr _NewPtr_flags(Size size, boolean_t system, boolean_t clear)
{
    if (fail_token)
        return NULL;
    ++live;
    return calloc(1, size);
}

void DisposPtr(Ptr ptr)
{
    assert(live);
    --live;
    free(ptr);
}

LONGINT
C_TickCount(void)
{
    return now;
}

OSErr GetFPos(INTEGER ref, LONGINT* position)
{
    assert(ref == 99);
    *position = mark;
    return noErr;
}

OSErr GetEOF(INTEGER ref, LONGINT* size)
{
    assert(ref == 99);
    *size = movie_used;
    return noErr;
}

OSErr SetFPos(INTEGER ref, INTEGER mode, LONGINT position)
{
    assert(ref == 99 && mode == fsFromStart);
    mark = position;
    return noErr;
}

OSErr FSRead(INTEGER ref, LONGINT* size, Ptr bytes)
{
    assert(ref == 99 && *size >= 0 && (uint32)*size <= movie_used - mark);
    if (fail_read)
        return ioErr;
    memcpy(bytes, movie_bytes + mark, *size);
    mark += *size;
    return noErr;
}

OSErr FSClose(INTEGER ref)
{
    assert(ref == 99);
    return noErr;
}

OSErr C_FSpOpenDF(FSSpecPtr spec, SignedByte perm, int16* ref)
{
    assert(spec && perm == fsRdPerm);
    *ref = CW(99);
    return noErr;
}

void C_GetGWorld(HIDDEN_CGrafPtr* port, HIDDEN_GDHandle* device)
{
    HIDDEN_RAW_SET(*port, RM(&target));
    HIDDEN_RAW_SET(*device, 0);
}

void C_SetGWorld(CGrafPtr port, GDHandle device)
{
    assert(port == &target && !device);
}

QDErr C_NewGWorld(HIDDEN_GWorldPtr* port, INTEGER depth, Rect* rect, CTabHandle colors, GDHandle device,
                  GWorldFlags flags)
{
    assert(depth == 32 && CW(rect->right) == 2 && CW(rect->bottom) == 2);
    HIDDEN_RAW_SET(*port, RM(&world));
    pixels_pointer = RM(&pixels);
    pixels.rowBytes = CWC(0x8008);
    return noErr;
}

void C_DisposeGWorld(GWorldPtr port)
{
    assert(port == &world);
    ++disposed;
}

PixMapHandle C_GetGWorldPixMap(GWorldPtr port)
{
    assert(port == &world);
    return (PixMapHandle)&pixels_pointer;
}

BOOLEAN
C_LockPixels(PixMapHandle pixels)
{
    return TRUE;
}

void C_UnlockPixels(PixMapHandle pixels)
{
}

Ptr C_GetPixBaseAddr(PixMapHandle pixels)
{
    return (Ptr)output;
}

void C_CopyBits(BitMap* source, BitMap* destination, const Rect* sr, const Rect* dr, INTEGER mode, RgnHandle mask)
{
    assert(source == PORT_BITS_FOR_COPY(&world));
    assert(destination == PORT_BITS_FOR_COPY(&target));
    assert(mode == srcCopy && !mask && CW(sr->right) == 2);
    ++draws;
}

int main(void)
{
    movie_fixture();
    Movie movie;
    assert(!C_EnterMovies());
    assert(!C_GetMoviesError() && !C_GetMoviesStickyError());
    assert(C_UpdateMovie(NULL) == paramErr);
    assert(C_GetMoviesStickyError() == paramErr);
    assert(C_GetMoviesError() == paramErr);
    assert(!C_GetMoviesError());
    fail_read = 1;
    assert(C_NewMovieFromFile(&movie, 99, NULL, NULL, 1, NULL) == ioErr);
    assert(C_GetMoviesError() == ioErr);
    assert(C_GetMoviesStickyError() == paramErr);
    C_ClearMoviesStickyError();
    assert(!C_GetMoviesStickyError());
    assert(C_NewMovieFromFile(&movie, 99, NULL, NULL, 1, NULL) == ioErr);
    C_ClearMoviesStickyError();
    assert(C_GetMoviesError() == ioErr);
    assert(!C_GetMoviesStickyError());
    fail_read = 0;
    FSSpec spec = {0};
    INTEGER refnum;
    assert(!C_OpenMovieFile(&spec, &refnum, fsRdPerm));
    assert(CW(refnum) == 99);
    assert(!C_CloseMovieFile(CW(refnum)));
    assert(!C_NewMovieFromFile(&movie, 99, NULL, NULL, 1, NULL));
    movie = MR(movie);
    assert(!C_GetMoviesError());
    assert(live == 1 && movie_count == 1 && mark == 17);
    assert(C_GetMoviePreferredRate(movie) == 0x10000);
    assert(!C_UpdateMovie(movie) && draws == 1 && disposed == 1);
    for (unsigned pixel = 0; pixel < 4; ++pixel)
        assert(output[pixel * 4] == 0 && output[pixel * 4 + 1] == 255 && output[pixel * 4 + 2] == 0 &&
               output[pixel * 4 + 3] == 0);
    C_StartMovie(movie);
    now = 31;
    C_MoviesTask(movie, 0);
    assert(!C_IsMovieDone(movie) && output[1] == 0 && output[2] == 255);
    C_StopMovie(movie);
    now = 1000;
    assert(!C_IsMovieDone(movie));
    C_StartMovie(movie);
    now = 1060;
    C_MoviesTask(NULL, 0);
    assert(C_IsMovieDone(movie) && output[3] == 255);
    C_GoToBeginningOfMovie(movie);
    assert(!C_IsMovieDone(movie));
    assert(C_PrerollMovie(movie, 91, 0x10000) == paramErr);
    assert(!C_PrerollMovie(movie, 30, 0x10000));
    C_DisposeMovie(movie);
    assert(!live && !movie_count);
    assert(C_UpdateMovie(movie) == paramErr && C_IsMovieDone(movie));
    C_DisposeMovie(movie);
    fail_read = 1;
    assert(C_NewMovieFromFile(&movie, 99, NULL, NULL, 1, NULL) == ioErr);
    assert(!movie && !live && mark == 17);
    fail_read = 0;
    fail_token = 1;
    assert(C_NewMovieFromFile(&movie, 99, NULL, NULL, 1, NULL) == memFullErr);
    assert(!movie && !live && mark == 17);
    fail_token = 0;
    assert(!C_NewMovieFromFile(&movie, 99, NULL, NULL, 1, NULL));
    C_ExitMovies();
    assert(!live && !movie_count && !movies);
    assert(!C_NewMovieFromFile(&movie, 99, NULL, NULL, 1, NULL));
    movie = MR(movie);
    now = UINT32_MAX - 10;
    C_StartMovie(movie);
    now = 20; /* TickCount wraps: 31 ticks have elapsed. */
    C_MoviesTask(movie, 0);
    assert(movie_lookup(movie)->position == 31 && output[2] == 255);
    C_SetMovieActive(movie, FALSE);
    unsigned previous_draws = draws;
    C_MoviesTask(movie, 0);
    assert(draws == previous_draws);
    C_ExitMovies();
    assert(!live && !movie_count && !movies);
    puts("Movies: owned files, frame pixels, timing, pause, completion, stale tokens and rollback passed");
    return 0;
}
