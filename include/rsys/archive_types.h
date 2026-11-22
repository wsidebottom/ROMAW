/*+===================================================================
  File:      archive_types.h

  Summary:   Reserved Finder archive type and extension associations.

  Classes:   None.

  Functions: Inline classification shared by Finder and the file type database.

  Origin:    ROMAW native archive handling policy.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/
#ifndef RSYS_ARCHIVE_TYPES_H
#define RSYS_ARCHIVE_TYPES_H
#include <stdint.h>
#include <string.h>

enum ROMAWArchiveKind {
    ROMAW_ARCHIVE_NONE,
    ROMAW_ARCHIVE_ZIP,
    ROMAW_ARCHIVE_STUFFIT,
    ROMAW_ARCHIVE_BINHEX,
    ROMAW_ARCHIVE_MACBINARY
};

static inline int romaw_archive_type(uint32_t type)
{
    switch (type) {
    case 0x5a495020:
    case 0x5a697020:
    case 0x705a4950:
        return ROMAW_ARCHIVE_ZIP;
    case 0x53495421:
    case 0x53495444:
        return ROMAW_ARCHIVE_STUFFIT;
    case 0x42696e48:
        return ROMAW_ARCHIVE_BINHEX;
    case 0x6d42494e:
        return ROMAW_ARCHIVE_MACBINARY;
    default:
        return ROMAW_ARCHIVE_NONE;
    }
}

static inline int romaw_archive_extension(const char* extension)
{
    char text[16];
    if (!extension)
        return 0;
    if (*extension == '.')
        ++extension;
    size_t i;
    for (i = 0; extension[i] && i + 1 < sizeof text; ++i) {
        unsigned char c = (unsigned char)extension[i];
        text[i] = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
    }
    if (extension[i])
        return 0;
    text[i] = 0;
    if (!strcmp(text, "zip"))
        return ROMAW_ARCHIVE_ZIP;
    if (!strcmp(text, "sit") || !strcmp(text, "sit5"))
        return ROMAW_ARCHIVE_STUFFIT;
    if (!strcmp(text, "hqx") || !strcmp(text, "bhx") || !strcmp(text, "binhex"))
        return ROMAW_ARCHIVE_BINHEX;
    if (!strcmp(text, "bin") || !strcmp(text, "macbin") || !strcmp(text, "macbinary"))
        return ROMAW_ARCHIVE_MACBINARY;
    return 0;
}

static inline const char* romaw_archive_description(int kind)
{
    switch (kind) {
    case ROMAW_ARCHIVE_ZIP:
        return "ZIP archive";
    case ROMAW_ARCHIVE_STUFFIT:
        return "StuffIt archive";
    case ROMAW_ARCHIVE_BINHEX:
        return "BinHex archive";
    case ROMAW_ARCHIVE_MACBINARY:
        return "MacBinary archive";
    default:
        return "";
    }
}
#endif
