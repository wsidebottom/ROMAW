/*+===================================================================
  File:      probe.cpp

  Summary:   Implements probe support for disk images package support.

  Classes:   None identified in this file.

  Functions: main

  Origin:    Existing repository source in packages/disk-images/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "romaw_images.h"
#include <cstdio>

int main(int argc, char** argv)
{
    if (argc != 3)
        return 2;
    char error[1024];
    auto s = romaw_images_prepare(argv[1], argv[2], nullptr, error, sizeof error);
    if (!s) {
        std::fprintf(stderr, "%s\n", error);
        return 1;
    }
    for (unsigned i = 0; i < romaw_images_count(s); ++i)
        std::printf("%d|%s|%s\n", romaw_images_kind(s, i), romaw_images_name(s, i), romaw_images_path(s, i));
    romaw_images_close(s, 0);
    return 0;
}
