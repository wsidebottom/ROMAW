/*+===================================================================
  File:      system_resource_delta_test.c

  Summary:   Implements system resource delta test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#define NDEBUG 1
#include "../../resources/system_resources.c"
#undef NDEBUG
#undef main
#include <assert.h>

int main(int argc, char** argv)
{
    saved_resource r, decoded, found;
    unsigned char* bytes = malloc(8);
    size_t length = 8, offset;
    unsigned count = 0;
    int status;
    while ((status = resource_at(embedded_system, sizeof embedded_system, count, &r)) > 0) {
        assert(find_resource(embedded_system, sizeof embedded_system, &r, &found) == 1);
        assert(found.length == r.length && found.data == r.data);
        assert(append_saved(&bytes, &length, &r));
        ++count;
    }
    assert(status == 0 && count == 247);
    offset = 8;
    for (unsigned i = 0; i < count; ++i) {
        assert(resource_at(embedded_system, sizeof embedded_system, i, &r) == 1);
        assert(next_saved(bytes, length, &offset, &decoded) == 1);
        assert(decoded.length == r.length && decoded.id == r.id);
        assert(decoded.name_length == r.name_length);
        assert(!memcmp(decoded.data, r.data, r.length));
        assert(decoded.attributes == r.attributes);
    }
    assert(next_saved(bytes, length, &offset, &decoded) == 0);
    /* Every truncated override must be rejected before applying it. */
    for (size_t end = 9; end < length; ++end) {
        size_t position = 8;
        while ((status = next_saved(bytes, end, &position, &decoded)) > 0)
            ;
        assert(status == -1 || position == end);
    }
    /* ResEdit sees ordinary types, IDs, names, attributes and payloads. */
    memcpy(bytes, "ERS1", 4);
    put32(bytes + 4, length);
    unsigned char *fork = NULL, *restored = NULL;
    size_t fork_length = 0, restored_length = 0;
    assert(fork_from_saved(bytes, length, &fork, &fork_length));
    if (argc == 2) {
        FILE* output = fopen(argv[1], "wb");
        assert(output && fwrite(fork, 1, fork_length, output) == fork_length);
        assert(!fclose(output));
    }
    assert(saved_from_fork(fork, fork_length, &restored, &restored_length));
    offset = 8;
    for (unsigned i = 0; i < count; ++i) {
        assert(next_saved(restored, restored_length, &offset, &decoded) == 1);
        assert(find_resource(embedded_system, sizeof embedded_system, &decoded, &found) == 1);
        assert(decoded.length == found.length && decoded.id == found.id && decoded.attributes == found.attributes &&
               decoded.name_length == found.name_length);
        assert(!memcmp(decoded.data, found.data, found.length));
        if (found.name_length > 0)
            assert(!memcmp(decoded.name, found.name, found.name_length));
    }
    assert(next_saved(restored, restored_length, &offset, &decoded) == 0);
    for (size_t end = 0; end < fork_length; ++end) {
        unsigned char* invalid = NULL;
        size_t invalid_length = 0;
        assert(!saved_from_fork(fork, end, &invalid, &invalid_length));
        assert(!invalid && !invalid_length);
    }
    free(fork);
    free(restored);
    length = 8;
    r.length = UINT32_MAX;
    assert(append_saved(&bytes, &length, &r));
    offset = 8;
    assert(next_saved(bytes, length, &offset, &decoded) == 1);
    assert(decoded.length == UINT32_MAX);
    assert(resource_at(embedded_system, 15, 0, &r) == -1);
    assert(resource_at(embedded_system, sizeof embedded_system - 1, 0, &r) == -1);
    put32(bytes + 4, length);
    assert(fork_from_saved(bytes, length, &fork, &fork_length));
    assert(resource_at(fork, fork_length, 0, &decoded) == 1);
    assert(!memcmp(decoded.type, "ExDl", 4));
    assert(saved_from_fork(fork, fork_length, &restored, &restored_length));
    offset = 8;
    assert(next_saved(restored, restored_length, &offset, &decoded) == 1);
    assert(decoded.length == UINT32_MAX && decoded.id == r.id && !memcmp(decoded.type, r.type, 4));
    free(fork);
    free(restored);
    /* Empty overrides still form an editable, empty resource fork. */
    put32(bytes + 4, 8);
    assert(fork_from_saved(bytes, 8, &fork, &fork_length));
    assert(resource_at(fork, fork_length, 0, &decoded) == 0);
    free(fork);
    free(bytes);
    puts("All 247 embedded resources, override records and bounded parsing passed");
    return 0;
}
