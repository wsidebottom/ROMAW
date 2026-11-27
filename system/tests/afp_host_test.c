/*+===================================================================
  File:      afp_host_test.c

  Summary:   Verify a native AFP guest login and volume open over TCP.

  Classes:   None.

  Functions: main.

  Origin:    ROMAW interoperability test for a real AFP server.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include <stdio.h>
#include <string.h>
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
    static romaw_afp_volume_list volumes;
    error = romaw_afp_list_volumes(session, &volumes);
    if (!error) {
        unsigned index = 0;
        while (index < volumes.count && strcmp(volumes.items[index].name, "Macintosh"))
            ++index;
        if (index == volumes.count)
            error = ROMAW_AFP_TRANSPORT_ERROR;
        else
            error = romaw_afp_open_volume(session, volumes.items[index].name, &volume);
    }
    if (!error)
        error = romaw_afp_close_volume(session, volume);
    romaw_afp_close(session);
    if (error) {
        fprintf(stderr, "AFP volume test returned %ld\n", (long)error);
        return 1;
    }
    puts("PASS native AFP guest login, enumerate, open and close Macintosh");
    return 0;
}
