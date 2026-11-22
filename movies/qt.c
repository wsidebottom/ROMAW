/*+===================================================================
  File:      qt.c

  Summary:   Built-in Movie Toolbox. Original bounded raw-RGB playback implementation.

  Classes:   None.

  Functions: No externally linked function definitions.

  Origin:    Existing repository source in executor/src/services; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Built-in Movie Toolbox. Original bounded raw-RGB playback implementation. */
#include "rsys/common.h"
#include "QuickTime.h"
#include "MemoryMgr.h"
#include "OSUtil.h"
#include "OSEvent.h"
#include "ToolboxEvent.h"
#include "rsys/cquick.h"
#include "rsys/movie.h"

typedef struct movie_state {
    struct movie_state* next;
    Movie token;
    movie_info info;
    uint8* data;
    Rect box;
    CGrafPtr port;
    GDHandle device;
    uint32 position, anchor, clock;
    int active, running;
} movie_state;

static movie_state* movies;
static unsigned movie_count;
static OSErr movie_error, movie_sticky_error;

static OSErr movie_result(OSErr error)
{
    movie_error = error;
    if (error && !movie_sticky_error)
        movie_sticky_error = error;
    return error;
}

P0(PUBLIC pascal trap, OSErr, GetMoviesError)
{
    OSErr error = movie_error;
    movie_error = noErr;
    return error;
}

P0(PUBLIC pascal trap, OSErr, GetMoviesStickyError)
{
    return movie_sticky_error;
}

P0(PUBLIC pascal trap, void, ClearMoviesStickyError)
{
    movie_sticky_error = noErr;
}

static movie_state* movie_lookup(Movie token)
{
    for (movie_state* state = movies; state; state = state->next)
        if (state->token == token)
            return state;
    return NULL;
}

static void movie_advance(movie_state* state)
{
    if (!state->running)
        return;
    uint32 now = TickCount();
    uint64 position = state->anchor + (uint64)(uint32)(now - state->clock) * state->info.scale / 60;
    if (position >= state->info.duration) {
        state->position = state->info.duration;
        state->running = FALSE;
    }
    else
        state->position = position;
}

static OSErr movie_draw(movie_state* state)
{
    if (!state->active || !state->port)
        return noErr;
    unsigned sample = 0;
    while (sample + 1 < state->info.count && state->info.samples[sample + 1].start <= state->position)
        ++sample;
    Rect source = {0, 0, CW(state->info.height), CW(state->info.width)};
    HIDDEN_CGrafPtr saved_port;
    HIDDEN_GDHandle saved_device;
    HIDDEN_GWorldPtr world_out;
    GetGWorld(&saved_port, &saved_device);
    OSErr error = NewGWorld(&world_out, 32, &source, NULL, NULL, 0);
    SetGWorld(HIDDEN_NATIVE(saved_port), HIDDEN_NATIVE(saved_device));
    if (error)
        return error;
    GWorldPtr world = HIDDEN_NATIVE(world_out);
    PixMapHandle pixels = GetGWorldPixMap(world);
    if (!pixels || !LockPixels(pixels)) {
        DisposeGWorld(world);
        return memFullErr;
    }
    uint8* base = (uint8*)GetPixBaseAddr(pixels);
    if (!base)
        error = memFullErr;
    else {
        const uint8* input = state->data + state->info.samples[sample].offset;
        unsigned stride = PIXMAP_ROWBYTES(pixels) & 0x3fff;
        for (unsigned y = 0; y < state->info.height; ++y)
            for (unsigned x = 0; x < state->info.width; ++x) {
                uint8* pixel = base + y * stride + x * 4;
                pixel[0] = 0;
                memcpy(pixel + 1, input, 3);
                input += 3;
            }
        SetGWorld(state->port, state->device);
        CopyBits(PORT_BITS_FOR_COPY(world), PORT_BITS_FOR_COPY(state->port), &source, &state->box, srcCopy, NULL);
        SetGWorld(HIDDEN_NATIVE(saved_port), HIDDEN_NATIVE(saved_device));
    }
    UnlockPixels(pixels);
    DisposeGWorld(world);
    return error;
}

P0(PUBLIC pascal trap, OSErr, EnterMovies)
{
    return movie_result(noErr);
}

P1(PUBLIC pascal trap, void, DisposeMovie, Movie, movie)
{
    for (movie_state** link = &movies; *link; link = &(*link)->next)
        if ((*link)->token == movie) {
            movie_state* state = *link;
            *link = state->next;
            free(state->info.samples);
            free(state->data);
            DisposPtr((Ptr)state->token);
            free(state);
            --movie_count;
            movie_result(noErr);
            return;
        }
    movie_result(paramErr);
}

P0(PUBLIC pascal trap, void, ExitMovies)
{
    while (movies)
        C_DisposeMovie(movies->token);
    movie_result(noErr);
}

P2(PUBLIC pascal trap, void, MoviesTask, Movie, movie, LONGINT, maxmillisecs)
{
    OSErr error = noErr;
    if (movie && !movie_lookup(movie))
        error = paramErr;
    for (movie_state* state = movies; state; state = state->next)
        if (!movie || state->token == movie) {
            movie_advance(state);
            OSErr drawn = movie_draw(state);
            if (!error)
                error = drawn;
        }
    movie_result(error);
}

P3(PUBLIC pascal trap, OSErr, PrerollMovie, Movie, movie, TimeValue, time, Fixed, rate)
{
    movie_state* state = movie_lookup(movie);
    if (!state || time < 0 || (uint32)time > state->info.duration || rate != 0x10000)
        return movie_result(paramErr);
    state->position = state->anchor = time;
    state->clock = TickCount();
    return movie_result(noErr);
}

P2(PUBLIC pascal trap, void, SetMovieActive, Movie, movie, BOOLEAN, active)
{
    movie_state* state = movie_lookup(movie);
    if (state)
        state->active = !!active;
    movie_result(state ? noErr : paramErr);
}

P1(PUBLIC pascal trap, void, StartMovie, Movie, movie)
{
    movie_state* state = movie_lookup(movie);
    if (state && !state->running) {
        state->running = TRUE;
        state->anchor = state->position;
        state->clock = TickCount();
    }
    movie_result(state ? noErr : paramErr);
}

P1(PUBLIC pascal trap, void, StopMovie, Movie, movie)
{
    movie_state* state = movie_lookup(movie);
    if (state) {
        movie_advance(state);
        state->running = FALSE;
    }
    movie_result(state ? noErr : paramErr);
}

P1(PUBLIC pascal trap, void, GoToBeginningOfMovie, Movie, movie)
{
    movie_state* state = movie_lookup(movie);
    if (state) {
        state->position = state->anchor = 0;
        state->clock = TickCount();
    }
    movie_result(state ? noErr : paramErr);
}

P3(PUBLIC pascal trap, void, SetMovieGWorld, Movie, movie, CGrafPtr, port, GDHandle, device)
{
    movie_state* state = movie_lookup(movie);
    if (state) {
        state->port = port;
        state->device = device;
    }
    movie_result(state ? noErr : paramErr);
}

P1(PUBLIC pascal trap, OSErr, UpdateMovie, Movie, movie)
{
    movie_state* state = movie_lookup(movie);
    return movie_result(state ? movie_draw(state) : paramErr);
}

P1(PUBLIC pascal trap, INTEGER, GetMovieVolume, Movie, movie)
{
    movie_state* state = movie_lookup(movie);
    movie_result(state ? noErr : paramErr);
    return state ? 256 : 0;
}

P1(PUBLIC pascal trap, OSErr, CloseMovieFile, INTEGER, refnum)
{
    return movie_result(FSClose(refnum));
}

P1(PUBLIC pascal trap, BOOLEAN, IsMovieDone, Movie, movie)
{
    movie_state* state = movie_lookup(movie);
    movie_result(state ? noErr : paramErr);
    if (!state)
        return TRUE;
    movie_advance(state);
    return state->position == state->info.duration;
}

P6(PUBLIC pascal trap, OSErr, NewMovieFromFile, Movie*, moviep, INTEGER, refnum, INTEGER*, residp, StringPtr, resnamep,
   INTEGER, flags, BOOLEAN*, changedp)
{
    if (!moviep)
        return movie_result(paramErr);
    *moviep = NULL;
    if (movie_count == 32)
        return movie_result(memFullErr);
    LONGINT position, extent, count;
    OSErr error = GetFPos(refnum, &position);
    if (error)
        return movie_result(error);
    error = GetEOF(refnum, &extent);
    if (error)
        return movie_result(error);
    uint32 size = extent;
    if (!size || size > 64 * 1024 * 1024)
        return movie_result(-208);
    movie_state* state = calloc(1, sizeof *state);
    if (!state)
        return movie_result(memFullErr);
    state->data = malloc(size);
    if (!state->data)
        error = memFullErr;
    else {
        error = SetFPos(refnum, fsFromStart, 0);
        count = extent;
        if (!error)
            error = FSRead(refnum, &count, (Ptr)state->data);
        if (!error && count != extent)
            error = eofErr;
        if (!error)
            error = movie_parse(state->data, size, &state->info);
    }
    OSErr restored = SetFPos(refnum, fsFromStart, position);
    if (!error)
        error = restored;
    if (!error && !(state->token = (Movie)NewPtrClear(sizeof(MovieRecord))))
        error = memFullErr;
    if (error) {
        free(state->info.samples);
        free(state->data);
        free(state);
        return movie_result(error);
    }
    HIDDEN_CGrafPtr port;
    HIDDEN_GDHandle device;
    GetGWorld(&port, &device);
    state->port = HIDDEN_NATIVE(port);
    state->device = HIDDEN_NATIVE(device);
    state->active = flags & 1;
    state->box = (Rect){0, 0, CW(state->info.height), CW(state->info.width)};
    state->next = movies;
    movies = state;
    ++movie_count;
    *moviep = RM(state->token);
    if (residp)
        *residp = 0;
    if (resnamep)
        resnamep[0] = 0;
    if (changedp)
        *changedp = FALSE;
    return movie_result(noErr);
}

P1(PUBLIC pascal trap, Fixed, GetMoviePreferredRate, Movie, movie)
{
    movie_state* state = movie_lookup(movie);
    movie_result(state ? noErr : paramErr);
    return state ? 0x10000 : 0;
}

P2(PUBLIC pascal trap, void, GetMovieBox, Movie, movie, Rect*, boxp)
{
    movie_state* state = movie_lookup(movie);
    if (state && boxp)
        *boxp = state->box;
    movie_result(state && boxp ? noErr : paramErr);
}

P2(PUBLIC pascal trap, void, SetMovieBox, Movie, movie, const Rect*, boxp)
{
    movie_state* state = movie_lookup(movie);
    if (state && boxp && CW(boxp->right) > CW(boxp->left) && CW(boxp->bottom) > CW(boxp->top)) {
        state->box = *boxp;
        movie_result(noErr);
    }
    else
        movie_result(paramErr);
}

P3(PUBLIC pascal trap, ComponentInstance, NewMovieController, Movie, movie, const Rect*, rectangle, LONGINT, flags)
{
    /* Controller components are not implemented; never publish a fake instance. */
    movie_result(-4); /* unimpErr */
    return NULL;
}

P1(PUBLIC pascal trap, void, DisposeMovieController, ComponentInstance, controller)
{
    movie_result(-4); /* unimpErr */
}

P3(PUBLIC pascal trap, OSErr, OpenMovieFile, const FSSpec*, spec, INTEGER*, refnum, uint8, perm)
{
    if (!spec || !refnum)
        return movie_result(paramErr);
    *refnum = 0;
    return movie_result(FSpOpenDF((FSSpec*)spec, perm, refnum));
}
