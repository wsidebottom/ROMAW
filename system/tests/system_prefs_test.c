/*+===================================================================
  File:      system_prefs_test.c

  Summary:   Verify AppleDouble resource storage and data-fork preservation.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "../../resources/systemprefs.c"
#include <assert.h>

int main(int argc, char** argv)
{
    unsigned char* bytes;
    size_t size;
    unsigned char data[1024];
    char after[8192], resource[4096];
    assert(argc == 2);
    FILE* file;
    snprintf(resource, sizeof resource, "%s.rsrc", argv[1]);
    char* sidecar = resource_sidecar_path(resource);
    assert(sidecar);
    remove(resource);
    remove(sidecar);
    assert(system_resource_read(resource, &bytes, &size) && !bytes && !size);
    for (unsigned i = 0; i < sizeof data; ++i)
        data[i] = i;
    assert(system_resource_write(resource, data, sizeof data));
    assert(system_resource_read(resource, &bytes, &size));
    assert(size == sizeof data && !memcmp(bytes, data, size));
    free(bytes);
    file = fopen(resource, "rb");
    assert(file && fgetc(file) == EOF);
    fclose(file);
    file = fopen(sidecar, "rb");
    assert(file);
    assert(fread(after, 1, 82, file) == 82);
    fclose(file);
    assert(!memcmp(after + 50, "rsrcRSED", 8));
    assert(read_be32((unsigned char*)after + 38) == 2);
    assert(read_be32((unsigned char*)after + 46) == sizeof data);
    /* Existing data is never truncated, and rejected writes preserve the fork. */
    file = fopen(resource, "wb");
    assert(file);
    fputs("Keep data", file);
    fclose(file);
    assert(!system_resource_write(resource, NULL, 10));
    assert(system_resource_write(resource, data, 16));
    file = fopen(resource, "rb");
    assert(file && fread(after, 1, 9, file) == 9);
    assert(!memcmp(after, "Keep data", 9));
    fclose(file);
    assert(system_resource_read(resource, &bytes, &size));
    assert(size == 16 && !memcmp(bytes, data, size));
    free(bytes);
    /* A newly created PRAM file may have Finder information but no resource
       entry yet. Treat that valid metadata-only sidecar as an absent fork. */
    file = fopen(sidecar, "r+b");
    assert(file);
    assert(!fseek(file, 24, SEEK_SET));
    fputc(0, file);
    fputc(1, file);
    fclose(file);
    assert(system_resource_read(resource, &bytes, &size) && !bytes && !size);
    assert(system_resource_write(resource, data, 16));
    file = fopen(sidecar, "r+b");
    assert(file && !fseek(file, 46, SEEK_SET));
    const unsigned char empty_length[4] = {0};
    assert(fwrite(empty_length, 1, 4, file) == 4);
    fclose(file);
    assert(system_resource_read(resource, &bytes, &size) && !bytes && !size);
    assert(system_resource_write(resource, data, 16));
    /* Truncated and out-of-bounds AppleDouble entries fail without outputs. */
    file = fopen(sidecar, "r+b");
    assert(file);
    assert(!fseek(file, 46, SEEK_SET));
    fputc(0xff, file);
    fclose(file);
    assert(!system_resource_read(resource, &bytes, &size));
    assert(!bytes && !size);
    assert(!system_resource_write("/nonexistent-romaw-test-directory/rsrc", data, sizeof data));
    remove(resource);
    remove(sidecar);
    free(sidecar);
    puts("AppleDouble forks, "
         "rsrc/RSED metadata and malformed input passed");
    return 0;
}
