/*+===================================================================
  File:      fauxdbm.c

  Summary:   Implements fauxdbm support for host operating-system support.

  Classes:   None.

  Functions: dbm_close, dbm_fetch, dbm_firstkey, dbm_nextkey, dbm_open, dbm_store

  Origin:    Existing repository source in executor/src/host; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1998 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/*
 * The original plan was to use the Registry to simulate dbm, but
 * upon sober reflection, that was decided against, since a fried
 * registry equals a dead machine.  Instead, we're implementing an
 * incredibly hokey subset of the functionality that is tailored to
 * the specific way ROMlib calls the dbm routines.  Maybe we'll port
 * the BSD dbm routines sometime.
 */

#include "rsys/common.h"

#if defined(CYGWIN32)

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "rsys/fauxdbm.h"

PUBLIC DBM* dbm_open(const char* name, int flags, int mode)
{
    DBM* retval;
    char* open_mode;
    FILE* fp;

    retval = NULL;

    if (flags == O_RDONLY)
        open_mode = "rb";
    else
        open_mode = "a+b";

    fp = fopen(name, open_mode);
    if (fp != NULL) {
        retval = malloc(sizeof *retval);
        retval->fp = fp;
    }

    return retval;
}

PUBLIC datum dbm_firstkey(DBM* file)
{
    datum retval;

    if (!file) {
        retval.dptr = 0;
        retval.dsize = 0;
    }
    else {
        rewind(file->fp);
        retval = dbm_nextkey(file);
    }
    return retval;
}

#define KEY_ID "key"         /* 4 bytes long, counting NUL */
#define CONTENT_ID "content" /* 8 bytes long, counting NUL */

PRIVATE void read_datum(DBM* file)
{
    if (file) {
        char* p;

        p = file->buf;
        if (fread(p, 4, 1, file->fp) != 1)
            *(uint32*)p = 0;
        else {
            int to_read;

            to_read = sizeof KEY_ID + *(uint32*)p;
            p += 4;
            while (to_read % 4)
                ++to_read;
            if (p + to_read - file->buf <= (int)sizeof file->buf) {
                fread(p, to_read, 1, file->fp);
                p += to_read;
                if (fread(p, 4, 1, file->fp) != 1)
                    *(uint32*)p = 0;
                else {
                    to_read = sizeof CONTENT_ID + *(uint32*)p;
                    p += 4;
                    while (to_read % 4)
                        ++to_read;
                    if (p + to_read - file->buf <= (int)sizeof file->buf)
                        fread(p, to_read, 1, file->fp);
                }
            }
        }
    }
}

PUBLIC datum dbm_nextkey(DBM* file)
{
    datum retval;

    if (!file) {
        retval.dptr = 0;
        retval.dsize = 0;
    }
    else {
        read_datum(file);
        retval.dsize = *(uint32*)file->buf;
        if (retval.dsize)
            retval.dptr = file->buf + sizeof(uint32) + sizeof KEY_ID;
        else
            retval.dptr = 0;
    }
    return retval;
}

PUBLIC datum dbm_fetch(DBM* file, datum key)
{
    datum retval;
    int keysize;
    int offset;

    keysize = *(uint32*)file->buf;
    offset = sizeof(uint32) + sizeof(KEY_ID) + keysize;
    while (offset % 4)
        ++offset;
    retval.dsize = *(uint32*)(file->buf + offset);
    if (retval.dsize)
        retval.dptr = file->buf + offset + sizeof(uint32) + sizeof CONTENT_ID;
    else
        retval.dptr = 0;
    return retval;
}

#define RAW_LENGTH(datum, id) (sizeof(uint32) + sizeof(id) + datum.dsize)

#define PADDING(n)               \
    ({                           \
        int _n = n;              \
        _n % 4 ? 4 - _n % 4 : 0; \
    })

PRIVATE void buf_from_datum(char** opp, datum d, const char* tag)
{
    char* op;

    op = *opp;
    *(uint32*)op = (uint32)d.dsize;
    op += sizeof(uint32);
    memcpy(op, tag, strlen(tag) + 1);
    op += strlen(tag) + 1;
    memcpy(op, d.dptr, d.dsize);
    op += d.dsize;
    while ((op - *opp) % 4)
        ++op;
    *opp = op;
}

PUBLIC int dbm_store(DBM* file, datum key, datum content, int flags)
{
    int retval;

    retval = -1;
    if (file) {
        int key_raw_length, content_raw_length;
        int key_padding, content_padding;

        key_raw_length = RAW_LENGTH(key, KEY_ID);
        content_raw_length = RAW_LENGTH(content, CONTENT_ID);

        key_padding = PADDING(key_raw_length);
        content_padding = PADDING(content_raw_length);

        if (key_raw_length + key_padding + content_raw_length + content_padding <= (int)sizeof file->buf) {
            char* op;

            op = file->buf;
            buf_from_datum(&op, key, KEY_ID);
            buf_from_datum(&op, content, CONTENT_ID);
            if (fwrite(file->buf, op - file->buf, 1, file->fp) == 1)
                retval = 0;
        }
    }
    return retval;
}

PUBLIC void dbm_close(DBM* file)
{
    if (file) {
        fclose(file->fp);
        free(file);
    }
}

#endif
