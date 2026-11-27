/*+===================================================================
  File:      afp_host_test.c

  Summary:   Verify native AFP login, catalog enumeration and file data over TCP.

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
    if (!error) {
        romaw_afp_entry entry;
        error = romaw_afp_lookup(session, volume, 2, NULL, 0, &entry);
        if (!error && (!entry.directory || entry.node_id != 2 || strcmp(entry.name, "Macintosh")))
            error = ROMAW_AFP_TRANSPORT_ERROR;
        int read_data = 0;
        for (uint32_t index = 1; !error && index <= 64; ++index) {
            error = romaw_afp_enumerate(session, volume, 2, index, &entry);
            if (error)
                break;
            if (!entry.node_id || !entry.name[0]) {
                error = ROMAW_AFP_TRANSPORT_ERROR;
                break;
            }
            if (entry.directory)
                continue;
            uint16_t fork;
            uint32_t length;
            error = romaw_afp_open_read_fork(session, volume, 2, entry.name, 0, &fork, &length);
            if (error)
                break;
            unsigned char data;
            unsigned received;
            error = romaw_afp_read_fork(session, fork, 0, &data, 1, &received);
            if (error == -5009)
                error = 0;
            int32_t closed = romaw_afp_close_fork(session, fork);
            if (!error)
                error = closed;
            if (error)
                break;
            if (received == 1 && length) {
                printf("PASS read a file byte from root catalog entry %lu\n", (unsigned long)index);
                read_data = 1;
                break;
            }
        }
        if (!error && !read_data)
            error = ROMAW_AFP_TRANSPORT_ERROR;
        int32_t closed = romaw_afp_close_volume(session, volume);
        if (!error)
            error = closed;
    }
    romaw_afp_close(session);
    if (error) {
        fprintf(stderr, "AFP volume test returned %ld\n", (long)error);
        return 1;
    }
    puts("PASS native AFP guest login, enumerate, open and close Macintosh");
    return 0;
}
