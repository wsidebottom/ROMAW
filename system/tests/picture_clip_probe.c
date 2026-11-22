/*+===================================================================
  File:      picture_clip_probe.c

  Summary:   Reproduce ResEdit's translated PICT with a nearly infinite clip.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Reproduce ResEdit's translated PICT with a nearly infinite clip. */
#include <Multiverse.h>
#include <stdio.h>
#include <string.h>

static void put_word(unsigned char* data, int* length, short value)
{
    data[(*length)++] = (unsigned short)value >> 8;
    data[(*length)++] = value;
}

int main(void)
{
    GrafPort port;
    GrafPtr saved;
    unsigned char pixels[512], data[64];
    Rect bounds = {0, 0, 64, 64};
    Rect destinations[] = {{8, 8, 24, 24}, {8, 8, 40, 40}, {-8, -8, 24, 24}};
    Rect clips[] = {{-32767, -32767, 32767, 32767}, {0, 0, 8, 8}, {0, 0, 0, 0}};
    Handle picture;
    RgnHandle original;
    int failures = 0, i, j, length;
    short report;
    long count;
    char text[80];
    Str255 path = "\pD:Source:ROMAW:build:picture-clip-probe.txt";
    HCreate(0, 0, path, 'PcPr', 'TEXT');
    if (HOpen(0, 0, path, fsRdWrPerm, &report))
        return 1;
    SetEOF(report, 0);
    InitGraf(&qd.thePort);
    GetPort(&saved);
    OpenPort(&port);
    port.portBits.baseAddr = (Ptr)pixels;
    port.portBits.rowBytes = 8;
    port.portBits.bounds = bounds;
    port.portRect = bounds;
    RectRgn(port.visRgn, &bounds);
    ClipRect(&bounds);
    original = NewRgn();
    GetClip(original);
    picture = NewHandle(64);
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j) {
            int k;
            short frame[] = {0, 0, 16, 16};
            short paint[] = {2, 2, 14, 14};
            length = 0;
            put_word(data, &length, 0);
            for (k = 0; k < 4; ++k)
                put_word(data, &length, frame[k]);
            data[length++] = 0x11;
            data[length++] = 1;
            data[length++] = 1;
            put_word(data, &length, 10);
            put_word(data, &length, clips[j].top);
            put_word(data, &length, clips[j].left);
            put_word(data, &length, clips[j].bottom);
            put_word(data, &length, clips[j].right);
            data[length++] = 0x31;
            for (k = 0; k < 4; ++k)
                put_word(data, &length, paint[k]);
            data[length++] = 0xff;
            data[0] = length >> 8;
            data[1] = length;
            memcpy(*picture, data, length);
            memset(pixels, 0, sizeof pixels);
            DrawPicture((PicHandle)picture, &destinations[i]);
            if (!EqualRgn(port.clipRgn, original))
                ++failures;
            /* This pixel is in both the paint and the finite clip in all sizes. */
            if (!!GetPixel(i == 2 ? 2 : 13, i == 2 ? 2 : 13) != (j != 2))
                ++failures;
            if (j == 1 && GetPixel(i == 0 ? 20 : i == 1 ? 30 : 18, i == 0 ? 20 : i == 1 ? 30 : 18))
                ++failures;
        }
    DisposeHandle(picture);
    DisposeRgn(original);
    ClosePort(&port);
    SetPort(saved);
    count = sprintf(text, "translated picture clip failures=%d\n", failures);
    FSWrite(report, &count, text);
    FSClose(report);
    return failures != 0;
}
