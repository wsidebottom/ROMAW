/*+===================================================================
  File:      syncint.c

  Summary:   Implements syncint support for Macintosh event handling and virtual interrupts.

  Classes:   None.

  Functions: syncint_init, syncint_post

  Origin:    Existing repository source in executor/src/events; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1995, 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"

/* We only have synchronous interrupts under syn68k. */
#if defined(SYN68K)

#include "rsys/syncint.h"
#include "rsys/blockinterrupts.h"

#if defined(CYGWIN32)
#include "win_queue.h"
#include "rsys/m68kint.h"
#endif

#if defined(__MINGW32__) && !defined(SDL)
typedef void* mingw_HANDLE;
typedef void* mingw_PVOID;
typedef unsigned long mingw_DWORD;
typedef int mingw_BOOL;
#define MINGW_WINAPI __attribute__((stdcall))
#define MINGW_WT_EXECUTEDEFAULT 0

typedef void(MINGW_WINAPI* mingw_WAITORTIMERCALLBACK)(mingw_PVOID, unsigned char);

extern mingw_BOOL MINGW_WINAPI CreateTimerQueueTimer(mingw_HANDLE* phNewTimer, mingw_HANDLE TimerQueue,
                                                     mingw_WAITORTIMERCALLBACK Callback, mingw_PVOID Parameter,
                                                     mingw_DWORD DueTime, mingw_DWORD Period, unsigned long Flags);
extern mingw_BOOL MINGW_WINAPI DeleteTimerQueueTimer(mingw_HANDLE TimerQueue, mingw_HANDLE Timer,
                                                     mingw_HANDLE CompletionEvent);

static mingw_HANDLE mingw_sync_timer;
static void handle_itimer_tick(int n);

static void MINGW_WINAPI mingw_sync_timer_proc(mingw_PVOID unused, unsigned char timer_fired)
{
    (void)unused;
    (void)timer_fired;
    mingw_sync_timer = NULL;
    handle_itimer_tick(0);
}
#endif

#if defined(SDL)

static Uint32 handle_sdltimer_tick(Uint32 n)
{
    //  fprintf (stderr, "T");
    //  fflush (stderr);
#if defined(CYGWIN32)
    win_queue(&cpu_state.interrupt_pending[M68K_EVENT_PRIORITY]);
#endif
    cpu_state.interrupt_pending[M68K_TIMER_PRIORITY] = TRUE;
    SET_INTERRUPT_STATUS(INTERRUPT_STATUS_CHANGED);
    //  SDL_mutexP (ROMlib_shouldbeawake_mutex);
    SDL_CondSignal(ROMlib_shouldbeawake_cond);
    //  SDL_mutexV (ROMlib_shouldbeawake_mutex);

#if !defined(SDL_TIMER_FIXED)
#warning returning 1 here to work around SDL timer bug(s)
    return 1;
#endif

    return 0;
}

#else

static void handle_itimer_tick(int n)
{
    cpu_state.interrupt_pending[M68K_TIMER_PRIORITY] = TRUE;
    SET_INTERRUPT_STATUS(INTERRUPT_STATUS_CHANGED);
}

#endif

int syncint_init(void)
{
#if defined(SDL)

    return (SDL_Init(SDL_INIT_TIMER) == 0);

#else /* !SDL */

#if defined(__MINGW32__)

    return TRUE;

#else

#if !defined(USE_BSD_SIGNALS)

    struct sigaction s;

    s.sa_handler = handle_itimer_tick;
    sigemptyset(&s.sa_mask);
    s.sa_flags = 0;
    s.sa_restorer = NULL;
    return (sigaction(SIGALRM, &s, NULL) == 0);

#else
    struct sigvec s;

    s.sv_handler = handle_itimer_tick;
    s.sv_mask = 0;
    s.sv_flags = 0;
    return sigvec(SIGALRM, &s, NULL) == 0;
#endif

#endif /* !__MINGW32__ */

#endif
}

/* Posting a delay of 0 will clear any pending interrupt. */
void syncint_post(unsigned long usecs)
{
#if defined(SDL)
    //  fprintf (stderr, "P(%ul)", usecs);
    //  fflush (stderr);
    SDL_SetTimer(usecs / 1000, handle_sdltimer_tick);
#elif defined(__MINGW32__)
    if (mingw_sync_timer) {
        DeleteTimerQueueTimer(NULL, mingw_sync_timer, (mingw_HANDLE)-1);
        mingw_sync_timer = NULL;
    }
    if (usecs) {
        mingw_DWORD msecs;

        msecs = usecs / 1000;
        if (!msecs)
            msecs = 1;
        CreateTimerQueueTimer(&mingw_sync_timer, NULL, mingw_sync_timer_proc, NULL, msecs, 0, MINGW_WT_EXECUTEDEFAULT);
    }
#else
    struct itimerval t;

    t.it_value.tv_sec = usecs / 1000000;
    t.it_value.tv_usec = usecs % 1000000;
    t.it_interval.tv_sec = 0;
    t.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &t, NULL);
#endif
}

#endif /* SYN68K */
