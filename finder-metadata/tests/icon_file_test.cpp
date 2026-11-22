/*+===================================================================
  File:      icon_file_test.cpp

  Summary:   Compare shared Windows ICO/ICNS decoding against independently generated RGBA fixtures.

  Classes:   None declared.

  Functions: main.

  Origin:    Original Executor regression fixture for shared icon-file decoding.

  Copyright and Legal notices: See finder-metadata/LICENSE.
===================================================================+*/
#include "finder_metadata/icons.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// Avoid the CRT's interactive abort dialog in unattended Windows checks.
#undef assert
#define assert(condition)                                                \
    do {                                                                 \
        if (!(condition)) {                                              \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
            std::exit(1);                                                \
        }                                                                \
    } while (0)

static std::vector<unsigned char> read(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    assert(file);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    std::ifstream manifest(std::string(argv[1]) + "/cases.txt");
    assert(manifest);
    std::string name;
    unsigned cases = 0;
    while (manifest >> name) {
        fprintf(stderr, "Checking %s\n", name.c_str());
        const std::string base = std::string(argv[1]) + "/" + name;
        auto bytes = read(base);
        for (unsigned size : {16u, 32u}) {
            auto wanted = read(base + "." + std::to_string(size) + ".rgba");
            std::vector<unsigned char> output(size * size * 4 + 32, 0xA5);
            assert(finder_decode_icon_file(bytes.data(), bytes.size(), size, output.data() + 16));
            assert(wanted.size() == size * size * 4);
            for (size_t i = 0; i < wanted.size(); ++i) {
                if (i % 4 != 3 && !wanted[i / 4 * 4 + 3])
                    continue; // RGB under a completely transparent pixel has no visible meaning.
                if (output[i + 16] != wanted[i])
                    fprintf(stderr, "%s size %u byte %zu: got %u expected %u\n", name.c_str(), size, i, output[i + 16],
                            wanted[i]);
                assert(output[i + 16] == wanted[i]);
            }
            for (unsigned i = 0; i < 16; ++i)
                assert(output[i] == 0xA5 && output[output.size() - 1 - i] == 0xA5);
        }
        FinderIcon family{};
        assert(finder_parse_icon_file(bytes.data(), bytes.size(), &family));
        assert((family.present & 63) == 63);
        std::array<unsigned char, 4096> output;
        for (size_t length = 0; length < bytes.size(); ++length) {
            output.fill(0xA5);
            // Some ICO truncations retain an entirely valid selected frame;
            // rejected inputs must never modify the caller's output buffer.
            if (!finder_decode_icon_file(bytes.data(), length, 32, output.data()))
                for (unsigned char byte : output)
                    assert(byte == 0xA5);
        }
        assert(!finder_decode_icon_file(bytes.data(), bytes.size(), 24, output.data()));
        assert(!finder_decode_icon_file(bytes.data(), bytes.size(), 32, nullptr));
        assert(!finder_parse_icon_file(bytes.data(), bytes.size(), nullptr));
        ++cases;
    }
    assert(cases >= 8);
    puts("PASS: Windows ICO/ICNS pixels, transparency, classic families, guards and truncated inputs");
}
