/*+===================================================================
  File:      intl_map.h

  Summary:   System 7 itlm mapping data. No guest pointers are retained while parsing. ifndef RSYS_INTL_MAP_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* System 7 itlm mapping data. No guest pointers are retained while parsing. */
#ifndef RSYS_INTL_MAP_H
#define RSYS_INTL_MAP_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const unsigned char* entries;
    unsigned maximum, fallback, count;
} romaw_intl_table;

static unsigned intl_map_word(const unsigned char* p)
{
    return (unsigned)p[0] * 256 + p[1];
}

static uint32_t intl_map_long(const unsigned char* p)
{
    return (uint32_t)intl_map_word(p) * 65536 + intl_map_word(p + 2);
}

static int intl_map_table(const unsigned char* data, size_t size, int language, romaw_intl_table* table)
{
    unsigned field = language ? 14 : 6;
    uint32_t offset, length;
    if (!data || size < 30 || intl_map_word(data + 2) != 0 || intl_map_word(data + 4) < (language ? 2 : 1))
        return 0;
    offset = intl_map_long(data + field);
    length = intl_map_long(data + field + 4);
    if (offset < 30 || offset > size || length < 6 || length > size - offset)
        return 0;
    data += offset;
    table->maximum = intl_map_word(data);
    table->fallback = intl_map_word(data + 2);
    table->count = intl_map_word(data + 4);
    if (table->maximum > 32767 || table->fallback > 32767 || table->count > (length - 6) / 4)
        return 0;
    table->entries = data + 6;
    return 1;
}

static unsigned intl_map_entry(const romaw_intl_table* table, unsigned code, unsigned* associated)
{
    unsigned position = 0x1000 + (code & 255);
    *associated = table->fallback;
    if (code > table->maximum)
        return 32767;
    for (unsigned i = 0; i < table->count; ++i)
        if (intl_map_word(table->entries + i * 4) == code) {
            *associated = intl_map_word(table->entries + i * 4 + 2);
            position = i;
            break;
        }
    return position;
}

static unsigned intl_script_position(int script, const romaw_intl_table* scripts)
{
    if (script == -1 || script == -2)
        script = 0; /* Both installed script selectors currently resolve to Roman. */
    if (script < 0 || script > (int)scripts->maximum)
        return 32767;
    /* Only Roman is installed/enabled. Other scripts follow installed scripts
     in code order, regardless of their order in itlm's script list. */
    return script == 0 ? 0 : 0x1000 + (script & 255);
}

static void intl_language_position(int language, int associated_script, int current, const romaw_intl_table* scripts,
                                   const romaw_intl_table* languages, unsigned* script_position,
                                   unsigned* language_position)
{
    unsigned script, ignored;
    if (language < 0) {
        if (language < -7 || language == -1)
            goto invalid;
        script = language >= -5 || associated_script == -1 || associated_script == -2 ? 0 : associated_script;
        if (script > 64)
            goto invalid;
        if ((language == -2 || language == -4 || language == -6) && script == 0)
            language = current;
        else {
            intl_map_entry(scripts, script, &ignored);
            language = ignored;
        }
        *script_position = intl_script_position(script, scripts);
        *language_position = intl_map_entry(languages, language, &ignored);
        return;
    }
    *language_position = intl_map_entry(languages, language, &script);
    if (language > (int)languages->maximum)
        script = 32767;
    *script_position = intl_script_position(script, scripts);
    return;
invalid:
    *script_position = *language_position = 32767;
}

static int intl_language_order(int first, int second, int first_script, int second_script, int current,
                               const romaw_intl_table* scripts, const romaw_intl_table* languages)
{
    unsigned as, al, bs, bl;
    intl_language_position(first, first_script, current, scripts, languages, &as, &al);
    intl_language_position(second, second_script, current, scripts, languages, &bs, &bl);
    return as < bs ? -1 : as > bs ? 1 : al < bl ? -1 : al > bl;
}
#endif
