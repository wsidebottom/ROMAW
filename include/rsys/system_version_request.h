/*+===================================================================
  File:      system_version_request.h

  Summary:   Bounded parsing of explicit classic Macintosh system-version requirements.

  Classes:   None.

  Functions: ROMlib_required_version_parse, ROMlib_required_version_message.

  Origin:    Original ROMAW startup requirement parser; no application-specific text resources.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/

#ifndef RSYS_SYSTEM_VERSION_REQUEST_H
#define RSYS_SYSTEM_VERSION_REQUEST_H

#include <stddef.h>
#include <stdint.h>

static inline uint32_t ROMlib_required_version_parse(const char* text, size_t size, size_t* consumed)
{
    size_t at = 0;
    unsigned parts[3] = {0};
    for (unsigned part = 0; part < 3; ++part) {
        if (at >= size || text[at] < '0' || text[at] > '9')
            return 0;
        parts[part] = text[at++] - '0';
        if (at == size || text[at] != '.')
            break;
        /* A trailing full stop belongs to the sentence. */
        if (at + 1 == size || text[at + 1] < '0' || text[at + 1] > '9')
            break;
        if (part == 2)
            return 0;
        ++at;
    }
    if (at < size && ((text[at] >= '0' && text[at] <= '9') || (text[at] >= 'A' && text[at] <= 'Z') ||
                      (text[at] >= 'a' && text[at] <= 'z')))
        return 0;
    uint32_t version = parts[0] << 8 | parts[1] << 4 | parts[2];
    if (version < 0x600 || version > 0x922)
        return 0;
    if (consumed)
        *consumed = at;
    return version;
}

static inline uint32_t ROMlib_required_version_message(const char* text, size_t size)
{
    static const char* const phrases[] = {
        "requires mac os ",
        "requires system ",
        "requires mac os version ",
        "requires system version ",
        "requires system software ",
        "requires system software version ",
        "needs mac os ",
        "needs system ",
        "can only run on mac os ",
        "only runs on mac os ",
        "must be running mac os ",
    };
    for (size_t at = 0; at < size; ++at) {
        if (at && ((text[at - 1] >= 'a' && text[at - 1] <= 'z') || (text[at - 1] >= 'A' && text[at - 1] <= 'Z')))
            continue;
        for (size_t phrase = 0; phrase < sizeof phrases / sizeof *phrases; ++phrase) {
            size_t n = 0;
            while (phrases[phrase][n] && at + n < size) {
                unsigned char c = text[at + n];
                if (c >= 'A' && c <= 'Z')
                    c += 'a' - 'A';
                if (c != (unsigned char)phrases[phrase][n])
                    break;
                ++n;
            }
            if (!phrases[phrase][n]) {
                uint32_t version = ROMlib_required_version_parse(text + at + n, size - at - n, NULL);
                if (version)
                    return version;
            }
        }
    }
    return 0;
}

#endif
