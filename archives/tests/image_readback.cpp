/*+===================================================================
  File:      image_readback.cpp

  Summary:   Independently read back extracted HFS files and forks through the disk-image library.

  Classes:   Standard filesystem paths.

  Functions: main.

  Origin:    ROMAW archive UI regression; does not use the mounted File Manager.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/
#include "romaw_images.h"
#include <cstdio>
#include <filesystem>

int main(int argc, char** argv)
{
    if (argc != 4)
        return 2;
    char error[2048]{};
    auto* image = romaw_images_open_direct_utf8(argv[1], error, sizeof error);
    if (!image) {
        std::fprintf(stderr, "%s\n", error);
        return 1;
    }
    bool ok = false;
    if (romaw_images_count(image) == 1) {
        auto source =
            (std::filesystem::u8path(romaw_images_path(image, 0)) / std::filesystem::u8path(argv[2])).u8string();
        ok = romaw_images_export_utf8(image, source.c_str(), argv[3], error, sizeof error);
    }
    if (!ok)
        std::fprintf(stderr, "%s\n", error);
    romaw_images_close(image, 0);
    return ok ? 0 : 1;
}
