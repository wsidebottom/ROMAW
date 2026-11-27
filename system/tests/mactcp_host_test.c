/*+===================================================================
  File:      mactcp_host_test.c

  Summary:   Exercise real Windows IPv4 resolution, ICMP payloads and cancellation.

  Classes:   None.

  Functions: main.

  Origin:    ROMAW MacTCP host networking regression suite.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rsys/host_mactcp.h"

int main(void)
{
    uint32_t address, mask, addresses[4];
    char canonical[255];
    assert(!host_mactcp_address(&address, &mask));
    assert(address && mask);
    assert(!host_mactcp_resolver_open());
    assert(!host_mactcp_resolver_open());
    assert(!host_mactcp_resolve("127.0.0.1", addresses, canonical));
    assert(addresses[0] == 0x7f000001);
    assert(!host_mactcp_resolve("localhost", addresses, canonical));
    assert(addresses[0] == 0x7f000001);
    host_mactcp_resolver_close();
    assert(!host_mactcp_resolve("127.0.0.1", addresses, canonical));
    host_mactcp_resolver_close();
    assert(host_mactcp_resolve("localhost", addresses, canonical) == -23047);

    unsigned char payload[513];
    for (unsigned i = 0; i < sizeof payload; ++i)
        payload[i] = (i * 37) & 255;
    const unsigned lengths[] = {0, 1, 32, 513};
    for (unsigned i = 0; i < sizeof lengths / sizeof lengths[0]; ++i) {
        int error;
        host_mactcp_request* request = host_mactcp_echo(0x7f000001, payload, lengths[i], 1000, NULL, 0, &error);
        assert(request && !error);
        DWORD deadline = GetTickCount() + 5000;
        const void* reply = NULL;
        uint16_t length = 0;
        uint32_t milliseconds = 0;
        do {
            error = host_mactcp_result(request, &reply, &length, &milliseconds);
            if (error == 1)
                Sleep(1);
        } while (error == 1 && (LONG)(deadline - GetTickCount()) > 0);
        assert(!error && length == lengths[i]);
        assert(!length || !memcmp(reply, payload, length));
        uint16_t option_length = 999;
        const void* options = host_mactcp_echo_options(request, &option_length);
        assert(option_length <= 40 && (!option_length || options));
        host_mactcp_dispose(request);
    }
    int error;
    assert(!host_mactcp_echo(0x7f000001, payload, 65535, 1000, NULL, 0, &error) && error == -23006);
    assert(!host_mactcp_echo(0x7f000001, payload, 1, 1000, payload, 41, &error) && error == -23006);
    for (unsigned i = 0; i < 8; ++i) {
        host_mactcp_request* request = host_mactcp_echo(0xc0000201, payload, 32, 100, NULL, 0, &error);
        host_mactcp_dispose(request);
    }
    puts(
        "PASS MacTCP: local address, nested resolver lifetime, numeric/localhost DNS, four ICMP payload sizes, invalid lengths/options and pending cancellation");
    return 0;
}
