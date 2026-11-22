/*+===================================================================
  File:      host_speech.h

  Summary:   Host speech synthesis. Native objects never enter Macintosh memory. ifndef RSYS_HOST_SPEECH_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Host speech synthesis. Native objects never enter Macintosh memory. */
#ifndef RSYS_HOST_SPEECH_H
#define RSYS_HOST_SPEECH_H
#include <stddef.h>

typedef struct host_speech_channel host_speech_channel;

typedef struct {
    int busy, paused;
    unsigned position, length;
} host_speech_status;

int host_speech_voice_count(void);
int host_speech_voice_name(unsigned index, char* name, size_t capacity);
host_speech_channel* host_speech_open(unsigned index);
void host_speech_close(host_speech_channel* channel);
int host_speech_speak(host_speech_channel* channel, const char* text, size_t length);
int host_speech_queue(host_speech_channel* channel, const char* text, size_t length);
int host_speech_stop(host_speech_channel* channel);
int host_speech_pause(host_speech_channel* channel);
int host_speech_resume(host_speech_channel* channel);
int host_speech_get_status(host_speech_channel* channel, host_speech_status* status);
int host_speech_set_rate(host_speech_channel* channel, int rate);
int host_speech_set_volume(host_speech_channel* channel, unsigned volume);
int host_speech_output_file(host_speech_channel* channel, const char* path);
void host_speech_shutdown(void);
#endif
