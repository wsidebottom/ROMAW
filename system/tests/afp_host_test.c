/*+===================================================================
  File:      afp_host_test.c

  Summary:   Verify a native AFP guest login and volume open over TCP.

  Classes:   None.

  Functions: main.

  Origin:    ROMAW interoperability test for a real AFP server.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include <stdio.h>
#include "rsys/host_afp.h"

int main(void)
{
    int32_t error;
    romaw_afp_session* session = romaw_afp_connect(0x0a000072, 548, &error);
    if (!session) {
        fprintf(stderr, "AFP connect returned %ld\n", (long)error);
        return 1;
    }
    uint16_t volume;
    error = romaw_afp_open_volume(session, "Macintosh", &volume);
    if (!error)
        error = romaw_afp_close_volume(session, volume);
    romaw_afp_close(session);
    if (error) {
        fprintf(stderr, "AFP volume test returned %ld\n", (long)error);
        return 1;
    }
    puts("PASS native AFP guest login, open and close Macintosh");
    return 0;
}
