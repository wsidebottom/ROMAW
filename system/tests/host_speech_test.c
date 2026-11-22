/*+===================================================================
  File:      host_speech_test.c

  Summary:   Exercise real local SAPI rendering to a disposable WAV, without speakers.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Exercise real local SAPI rendering to a disposable WAV, without speakers. */
#include <windows.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "rsys/host_speech.h"

int main(int argc, char** argv)
{
    assert(argc == 2 || (argc == 3 && !strcmp(argv[2], "--single")));
    int voices = host_speech_voice_count();
    assert(voices > 0);
    char name[256];
    assert(host_speech_voice_name(1, name, sizeof name) && *name);
    assert(!host_speech_voice_name(0, name, sizeof name));
    assert(!host_speech_voice_name(voices + 1, name, sizeof name));
    assert(!host_speech_open(voices + 1));
    host_speech_channel* channel = host_speech_open(1);
    assert(channel);
    assert(host_speech_output_file(channel, argv[1]));
    assert(host_speech_set_rate(channel, 0));
    assert(host_speech_set_volume(channel, 75));
    assert(!host_speech_set_rate(channel, 11));
    assert(!host_speech_set_volume(channel, 101));
    assert(!host_speech_speak(channel, NULL, 1));
    assert(!host_speech_speak(channel, "x", (size_t)-1));
    const char text[] = "Kid Pix speech synthesis. Caf\216. <not XML>.";
    assert(host_speech_speak(channel, text, sizeof text - 1));
    if (argc == 2)
        assert(host_speech_queue(channel, "Another buffer.", 15));
    host_speech_status status;
    DWORD started = GetTickCount();
    do {
        assert(host_speech_get_status(channel, &status));
        assert(GetTickCount() - started < 15000);
        if (status.busy)
            Sleep(10);
    } while (status.busy);
    assert(host_speech_pause(channel));
    assert(host_speech_pause(channel));
    assert(host_speech_get_status(channel, &status) && status.paused);
    assert(host_speech_resume(channel));
    assert(host_speech_resume(channel));
    assert(host_speech_get_status(channel, &status) && !status.paused);
    host_speech_close(channel);
    host_speech_close(NULL);
    host_speech_shutdown();
    assert(host_speech_voice_count() == voices);
    host_speech_shutdown();
    puts("Host speech: voices, real WAV synthesis, controls and cleanup passed");
    return 0;
}
