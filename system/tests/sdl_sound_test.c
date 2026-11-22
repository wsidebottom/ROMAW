/*+===================================================================
  File:      sdl_sound_test.c

  Summary:   Exercise the production SDL callback/worker handshake without audio hardware.

  Classes:   None.

  Functions: _gui_fatal, callback_install, main, sound_callback, syn68k_guest_to_host

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Exercise the production SDL callback/worker handshake without audio hardware. */
#include <SDL/SDL.h>
static int test_audio_init(const char*);
static int test_open_audio(SDL_AudioSpec*, SDL_AudioSpec*);
static char* test_audio_name(char*, int);
static void test_pause_audio(int);
static void test_close_audio(void);
#define SDL_AudioInit test_audio_init
#define SDL_OpenAudio test_open_audio
#define SDL_AudioDriverName test_audio_name
#define SDL_PauseAudio test_pause_audio
#define SDL_CloseAudio test_close_audio
#include "../../sound/sdl/sdl-sound.c"
#undef SDL_AudioInit
#undef SDL_OpenAudio
#undef SDL_AudioDriverName
#undef SDL_PauseAudio
#undef SDL_CloseAudio
#undef main
#undef NDEBUG
#include <assert.h>

CPUState cpu_state;
sound_driver_t sound_driver;
boolean_t sound_disabled_p;
uintptr_t ROMlib_offset;
uint32 syn68k_address_mask = 0xFFFFFFFF;
unsigned syn68k_host_alias_count;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;
static unsigned audio_api_calls;
static uint32 sound_vector;
static uint8 guest_memory[256];

static int test_audio_init(const char* name)
{
    ++audio_api_calls;
    return 0; /* SDL can report success with no selected driver. */
}

static char* test_audio_name(char* name, int size)
{
    return NULL;
}

static int test_open_audio(SDL_AudioSpec* desired, SDL_AudioSpec* obtained)
{
    abort();
}

static void test_pause_audio(int paused)
{
    ++audio_api_calls;
}

static void test_close_audio(void)
{
    ++audio_api_calls;
}

void* syn68k_guest_to_host(uint32 address)
{
    if (address == M68K_SOUND_VECTOR * 4)
        return &sound_vector;
    abort();
}

syn68k_addr_t callback_install(callback_handler_t fn, void* arg)
{
    assert(fn && !arg);
    return 0x1234;
}

syn68k_addr_t sound_callback(syn68k_addr_t pc, void* arg)
{
    abort();
}

static SDL_sem* callback_done;
static uint8 samples[64];

void _gui_fatal(const char* file, int line, const char* fn, const char* fmt, ...)
{
    fprintf(stderr, "Audio failure at %s:%d in %s: %s\n", file, line, fn, fmt);
    abort();
}

static void* run_callback(void* unused)
{
    sound_sdl_hunger_callback(NULL, samples, sizeof samples);
    SDL_SemPost(callback_done);
    return NULL;
}

static void wait_for_stream(void)
{
    Uint32 start = SDL_GetTicks();
    boolean_t ready;
    do {
        SDL_LockMutex(audio_mutex);
        ready = sdl_stream != NULL;
        SDL_UnlockMutex(audio_mutex);
        if (ready)
            return;
        SDL_Delay(1);
    } while (SDL_GetTicks() - start < 2000);
    assert(!"Audio callback failed to request a buffer");
}

static void finish_callback(pthread_t thread, uint8 expected)
{
    int i;
    assert(SDL_SemWaitTimeout(callback_done, 2000) == 0);
    pthread_join(thread, NULL);
    for (i = 0; i < sizeof samples; ++i)
        assert(samples[i] == expected);
}

int main(int argc, char** argv)
{
    pthread_t callback_thread;
    int i;
    assert(SDL_Init(SDL_INIT_TIMER) == 0);
    audio_mutex = SDL_CreateMutex();
    audio_ready = SDL_CreateCond();
    audio_filled = SDL_CreateCond();
    callback_done = SDL_CreateSemaphore(0);
    assert(audio_mutex && audio_ready && audio_filled && callback_done);
    assert(sem_init(&semid, 0, 0) == 0);
    num_samples = sizeof samples;
    memset(buf, 0x80, sizeof buf);
    assert(pthread_create(&audio_thread, NULL, loop, NULL) == 0);
    thread_started = TRUE;
    have_sound_p = TRUE;

    for (i = 0; i < 50; ++i) {
        struct hunger_info info;
        int j;

        sound_sdl_go(NULL);
        /* Enqueuing many commands must not create duplicate worker wakeups. */
        for (j = 0; j < 20; ++j)
            sound_sdl_go(NULL);
        assert(pthread_create(&callback_thread, NULL, run_callback, NULL) == 0);
        wait_for_stream();
        sound_sdl_hunger_start(NULL);
        info = sound_sdl_get_hunger_info(NULL);
        memset(info.buf + info.t2 % info.bufsize, 0x40 + i, num_samples);
        sound_sdl_hunger_finish(NULL);
        finish_callback(callback_thread, 0x40 + i);

        /* Stop after the next callback arrives but before another mix. */
        assert(pthread_create(&callback_thread, NULL, run_callback, NULL) == 0);
        wait_for_stream();
        sound_sdl_stop(NULL);
        finish_callback(callback_thread, 0x80);

        /* Further idle callbacks must return silence without waiting. */
        memset(samples, 0x33, sizeof samples);
        assert(pthread_create(&callback_thread, NULL, run_callback, NULL) == 0);
        finish_callback(callback_thread, 0x80);
    }

    sound_sdl_go(NULL);
    assert(pthread_create(&callback_thread, NULL, run_callback, NULL) == 0);
    wait_for_stream();
    sound_sdl_stop(NULL);
    finish_callback(callback_thread, 0x80);
    sound_sdl_shutdown(NULL);
    SDL_DestroySemaphore(callback_done);
    ROMlib_set_sdl_audio_driver_name("unavailable-test-driver");
    audio_api_calls = 0;
    assert(!sound_sdl_init(&sound_driver));
    assert(audio_api_calls == 1); /* No fallback OpenAudio call. */
    assert(argc == 2);
    char capture_environment[2048];
    assert(snprintf(capture_environment, sizeof capture_environment, "ROMAW_TEST_AUDIO_CAPTURE=%s", argv[1]) <
           sizeof capture_environment);
    assert(!putenv(capture_environment));
    ROMlib_set_sdl_audio_driver_name("test");
    audio_api_calls = 0;
    ROMlib_offset = (uintptr_t)guest_memory;
    assert(sound_sdl_init(&sound_driver));
    assert(*(uint32*)(guest_memory + M68K_SOUND_VECTOR * 4) == CL(0x1234));
    sound_sdl_go(&sound_driver);
    wait_for_stream();
    sound_sdl_hunger_start(&sound_driver);
    struct hunger_info info = sound_sdl_get_hunger_info(&sound_driver);
    memset(info.buf + info.t2 % info.bufsize, 0x5c, num_samples);
    sound_sdl_hunger_finish(&sound_driver);
    sound_sdl_shutdown(&sound_driver);
    assert(!audio_api_calls && !output_thread_started && !capture_file);
    FILE* captured = fopen(argv[1], "rb");
    assert(captured);
    uint8 block[BUFSIZE];
    unsigned mixed = 0;
    size_t count;
    while ((count = fread(block, 1, sizeof block, captured))) {
        assert(count == sizeof block);
        assert(block[0] == 0x80 || block[0] == 0x5c);
        mixed += block[0] == 0x5c;
        for (unsigned j = 1; j < sizeof block; ++j)
            assert(block[j] == block[0]);
    }
    assert(mixed == 1 && !ferror(captured));
    fclose(captured);
    SDL_Quit();
    puts("SDL sound: playback, stop, restart and capture without host audio passed");
    return 0;
}
