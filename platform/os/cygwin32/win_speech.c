/*+===================================================================
  File:      win_speech.c

  Summary:   Local Windows SAPI synthesis; COM stays on the emulation thread.

  Classes:   None.

  Functions: host_speech_close, host_speech_get_status, host_speech_open, host_speech_output_file, host_speech_pause,
             host_speech_queue, host_speech_resume, host_speech_set_rate, host_speech_set_volume, host_speech_shutdown,
             host_speech_speak, host_speech_stop, host_speech_voice_count, host_speech_voice_name

  Origin:    Existing repository source in executor/src/config/os/cygwin32; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Local Windows SAPI synthesis; COM stays on the emulation thread. */
#define COBJMACROS
#include <windows.h>
/* ROMAW has a minimal mmsystem.h shim; request the real audio format
   declarations explicitly before SAPI. */
#include <mmreg.h>
#include <sapi.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include "rsys/host_speech.h"

struct host_speech_channel {
    struct host_speech_channel* next;
    ISpVoice* voice;
    ISpStream* file;
    int paused;
};

static IEnumSpObjectTokens* voices;
static host_speech_channel* channels;
static int com_owned, initialized, voice_count;
static int cleanup_registered;

int host_speech_voice_count(void)
{
    if (initialized)
        return voice_count;
    if (!cleanup_registered) {
        if (atexit(host_speech_shutdown))
            return 0;
        cleanup_registered = 1;
    }
    initialized = 1;
    HRESULT error = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(error) && error != RPC_E_CHANGED_MODE)
        return 0;
    com_owned = SUCCEEDED(error);
    ISpObjectTokenCategory* category = NULL;
    error = CoCreateInstance(&CLSID_SpObjectTokenCategory, NULL, CLSCTX_INPROC_SERVER, &IID_ISpObjectTokenCategory,
                             (void**)&category);
    if (SUCCEEDED(error))
        error = ISpObjectTokenCategory_SetId(category, SPCAT_VOICES, FALSE);
    if (SUCCEEDED(error))
        error = ISpObjectTokenCategory_EnumTokens(category, NULL, NULL, &voices);
    if (category)
        ISpObjectTokenCategory_Release(category);
    ULONG count = 0;
    if (voices && SUCCEEDED(IEnumSpObjectTokens_GetCount(voices, &count)))
        voice_count = count > 32767 ? 32767 : count;
    return voice_count;
}

int host_speech_voice_name(unsigned index, char* name, size_t capacity)
{
    if (!name || !capacity || capacity > INT_MAX || !index || index > (unsigned)host_speech_voice_count())
        return 0;
    ISpObjectToken* token = NULL;
    WCHAR* description = NULL;
    HRESULT error = IEnumSpObjectTokens_Item(voices, index - 1, &token);
    if (SUCCEEDED(error))
        error = ISpObjectToken_GetStringValue(token, NULL, &description);
    int count = FAILED(error) ? 0 : WideCharToMultiByte(10000, 0, description, -1, name, (int)capacity, "?", NULL);
    CoTaskMemFree(description);
    if (token)
        ISpObjectToken_Release(token);
    return count > 0;
}

host_speech_channel* host_speech_open(unsigned index)
{
    if (!host_speech_voice_count() || index > (unsigned)voice_count)
        return NULL;
    host_speech_channel* channel = calloc(1, sizeof *channel);
    if (!channel)
        return NULL;
    HRESULT error =
        CoCreateInstance(&CLSID_SpVoice, NULL, CLSCTX_INPROC_SERVER, &IID_ISpVoice, (void**)&channel->voice);
    if (SUCCEEDED(error) && index) {
        ISpObjectToken* token = NULL;
        error = IEnumSpObjectTokens_Item(voices, index - 1, &token);
        if (SUCCEEDED(error))
            error = ISpVoice_SetVoice(channel->voice, token);
        if (token)
            ISpObjectToken_Release(token);
    }
    if (FAILED(error)) {
        if (channel->voice)
            ISpVoice_Release(channel->voice);
        free(channel);
        return NULL;
    }
    channel->next = channels;
    channels = channel;
    return channel;
}

int host_speech_stop(host_speech_channel* channel)
{
    if (!channel)
        return 0;
    HRESULT error = ISpVoice_Speak(channel->voice, NULL, SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL);
    if (channel->paused) {
        HRESULT resumed = ISpVoice_Resume(channel->voice);
        if (FAILED(resumed))
            error = resumed;
        else
            channel->paused = 0;
    }
    return SUCCEEDED(error);
}

void host_speech_close(host_speech_channel* channel)
{
    host_speech_channel** link = &channels;
    while (*link && *link != channel)
        link = &(*link)->next;
    if (!*link)
        return;
    *link = channel->next;
    host_speech_stop(channel);
    ISpVoice_Release(channel->voice);
    if (channel->file) {
        ISpStream_Close(channel->file);
        ISpStream_Release(channel->file);
    }
    free(channel);
}

static int speech_submit(host_speech_channel* channel, const char* text, size_t length, int interrupt)
{
    if (!channel || (length && !text) || length > INT_MAX || length > SIZE_MAX / sizeof(WCHAR) - 1)
        return 0;
    if (!length)
        return !interrupt || host_speech_stop(channel);
    WCHAR* wide = malloc((length + 1) * sizeof *wide);
    if (!wide)
        return 0;
    int count = MultiByteToWideChar(10000, 0, text, (int)length, wide, (int)length);
    if (!count) {
        free(wide);
        return 0;
    }
    for (int i = 0; i < count; ++i)
        if (!wide[i])
            wide[i] = L' ';
    wide[count] = 0;
    int stopped = !interrupt || host_speech_stop(channel);
    HRESULT error = stopped ? ISpVoice_Speak(channel->voice, wide, SPF_ASYNC | SPF_IS_NOT_XML, NULL) : E_FAIL;
    free(wide);
    return SUCCEEDED(error);
}

int host_speech_speak(host_speech_channel* channel, const char* text, size_t length)
{
    return speech_submit(channel, text, length, 1);
}

int host_speech_queue(host_speech_channel* channel, const char* text, size_t length)
{
    return speech_submit(channel, text, length, 0);
}

int host_speech_pause(host_speech_channel* channel)
{
    if (!channel)
        return 0;
    if (channel->paused)
        return 1;
    if (FAILED(ISpVoice_Pause(channel->voice)))
        return 0;
    channel->paused = 1;
    return 1;
}

int host_speech_resume(host_speech_channel* channel)
{
    if (!channel)
        return 0;
    if (!channel->paused)
        return 1;
    if (FAILED(ISpVoice_Resume(channel->voice)))
        return 0;
    channel->paused = 0;
    return 1;
}

int host_speech_get_status(host_speech_channel* channel, host_speech_status* status)
{
    if (!channel || !status)
        return 0;
    SPVOICESTATUS current = {0};
    if (FAILED(ISpVoice_GetStatus(channel->voice, &current, NULL)))
        return 0;
    /* GetStatus alone reports inactive for file output. The completion event
     covers both file rendering and playback through a Windows audio device. */
    HANDLE completed = ISpVoice_SpeakCompleteEvent(channel->voice);
    DWORD state = completed ? WaitForSingleObject(completed, 0) : WAIT_FAILED;
    if (state != WAIT_TIMEOUT && state != WAIT_OBJECT_0)
        return 0;
    status->busy = state == WAIT_TIMEOUT;
    status->paused = channel->paused;
    status->position = current.ulInputWordPos;
    status->length = current.ulInputWordLen;
    return 1;
}

int host_speech_set_rate(host_speech_channel* channel, int rate)
{
    return channel && rate >= -10 && rate <= 10 && SUCCEEDED(ISpVoice_SetRate(channel->voice, rate));
}

int host_speech_set_volume(host_speech_channel* channel, unsigned volume)
{
    return channel && volume <= 100 && SUCCEEDED(ISpVoice_SetVolume(channel->voice, volume));
}

int host_speech_output_file(host_speech_channel* channel, const char* path)
{
    if (!channel || !path || !host_speech_stop(channel))
        return 0;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    WCHAR* wide = count > 0 ? malloc((size_t)count * sizeof *wide) : NULL;
    if (!wide)
        return 0;
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, count);
    ISpStream* stream = NULL;
    HRESULT error = CoCreateInstance(&CLSID_SpStream, NULL, CLSCTX_INPROC_SERVER, &IID_ISpStream, (void**)&stream);
    static const GUID wave_format = {0xc31adbae, 0x527f, 0x4ff5, {0xa2, 0x30, 0xf6, 0x2b, 0xb6, 0x1f, 0xf7, 0x0c}};
    WAVEFORMATEX format = {WAVE_FORMAT_PCM, 1, 22050, 44100, 2, 16, 0};
    if (SUCCEEDED(error))
        error = ISpStream_BindToFile(stream, wide, SPFM_CREATE_ALWAYS, &wave_format, &format, 0);
    free(wide);
    if (SUCCEEDED(error))
        error = ISpVoice_SetOutput(channel->voice, (IUnknown*)stream, FALSE);
    if (SUCCEEDED(error)) {
        if (channel->file) {
            ISpStream_Close(channel->file);
            ISpStream_Release(channel->file);
        }
        channel->file = stream;
    }
    else if (stream)
        ISpStream_Release(stream);
    return SUCCEEDED(error);
}

void host_speech_shutdown(void)
{
    while (channels)
        host_speech_close(channels);
    if (voices)
        IEnumSpObjectTokens_Release(voices);
    voices = NULL;
    voice_count = initialized = 0;
    if (com_owned)
        CoUninitialize();
    com_owned = 0;
}
