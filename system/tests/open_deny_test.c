/*+===================================================================
  File:      open_deny_test.c

  Summary:   Implements open deny test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int16_t OSErr;
typedef uint8_t BOOLEAN;
typedef int boolean_t;

enum { FALSE, TRUE, fsCurPerm = 0, fsRdPerm = 1, fsWrPerm = 2, fsRdWrPerm = 3 };

typedef struct {
    struct {
        uintptr_t ioCompletion, ioNamePtr;
        int16_t ioVRefNum, ioRefNum, ioResult;
        uint8_t ioVersNum, ioPermssn;
        uint32_t ioMisc;
    } ioParam;

    struct {
        uint32_t ioDirID;
    } fileParam;
} HParamBlockRec, *HParmBlkPtr;

#define PP_RAW(value) (value)
static HParamBlockRec received;
static int selected_fork;
static BOOLEAN received_async;
static OSErr returned_error;

static OSErr mock_open(HParmBlkPtr pb, BOOLEAN async, int fork)
{
    received = *pb;
    selected_fork = fork;
    received_async = async;
    pb->ioParam.ioRefNum = 123;
    pb->ioParam.ioResult = returned_error;
    return returned_error;
}

static OSErr PBHOpen(HParmBlkPtr pb, BOOLEAN a)
{
    return mock_open(pb, a, 0);
}

static OSErr PBHOpenRF(HParmBlkPtr pb, BOOLEAN a)
{
    return mock_open(pb, a, 1);
}

static void fs_err_hook(OSErr error)
{
    assert(error == returned_error);
}

#include "open-deny-implementation.h"

int main(void)
{
    for (unsigned permission = 0; permission <= 255; ++permission)
        for (int fork = 0; fork <= 1; ++fork)
            for (unsigned async = 0; async <= 1; ++async)
                for (unsigned failure = 0; failure <= 1; ++failure) {
                    HParamBlockRec pb = {0};
                    pb.ioParam.ioCompletion = 0x123456;
                    pb.ioParam.ioNamePtr = 0x654322;
                    pb.ioParam.ioVRefNum = -17;
                    pb.ioParam.ioVersNum = 99;
                    pb.ioParam.ioPermssn = permission;
                    pb.ioParam.ioMisc = 0xabcdef;
                    pb.fileParam.ioDirID = 0x12345678;
                    HParamBlockRec expected = pb;
                    returned_error = failure ? -49 : 0;
                    assert(open_deny_fork(&pb, async, fork) == returned_error);
                    assert(selected_fork == fork && received_async == async);
                    assert(received.ioParam.ioCompletion == expected.ioParam.ioCompletion);
                    assert(received.ioParam.ioNamePtr == expected.ioParam.ioNamePtr);
                    assert(received.ioParam.ioVRefNum == expected.ioParam.ioVRefNum);
                    assert(received.fileParam.ioDirID == expected.fileParam.ioDirID);
                    assert(received.ioParam.ioPermssn == (permission & 3));
                    assert(!received.ioParam.ioVersNum && !received.ioParam.ioMisc);
                    expected.ioParam.ioRefNum = 123;
                    expected.ioParam.ioResult = returned_error;
                    assert(!memcmp(&pb, &expected, sizeof pb));
                }
    puts("data/resource deny opens, permissions and PB ownership passed");
    return 0;
}
