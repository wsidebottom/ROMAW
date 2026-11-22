/*+===================================================================
  File:      archive_test.cpp

  Summary:   Exercise the real StuffIt encoder, fork verification and safe failures.

  Classes:   romaw::stuffit::Entry.

  Functions: main.

  Origin:    ROMAW StuffIt bridge regression fixtures.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/
#include "romaw_stuffit.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace romaw::stuffit;

int main(int argc, char** argv)
{
    try {
        if (argc != 2)
            throw std::runtime_error("Supply the output .sit path");
        Entry folder;
        folder.name = "Folder";
        folder.folder = true;
        Entry nested = folder;
        nested.name = "Folder/Empty";
        Entry file;
        file.name = "Folder/caf\xc3\xa9";
        file.type = {'A', 'P', 'P', 'L'};
        file.creator = {'T', 'E', 'S', 'T'};
        file.flags = 0x4400;
        file.created = 3400000000u;
        file.modified = 3500000000u;
        file.data.resize(8193);
        file.resource.resize(4097);
        for (size_t i = 0; i < file.data.size(); ++i)
            file.data[i] = (i * 73 + i / 11) & 255;
        for (size_t i = 0; i < file.resource.size(); ++i)
            file.resource[i] = (i * 31 + i / 17) & 255;
        Entry empty;
        empty.name = "Folder/Zero";
        std::vector<Entry> entries{folder, nested, file, empty};
        for (size_t size : {0u, 1u, 2u, 31u, 32u, 255u, 256u, 4095u, 4096u, 8193u}) {
            Entry random;
            random.name = "Folder/Random" + std::to_string(size);
            uint32_t state = uint32_t(size + 1);
            auto fill = [&](Bytes& bytes, size_t length) {
                bytes.resize(length);
                for (auto& byte : bytes) {
                    state ^= state << 13;
                    state ^= state >> 17;
                    state ^= state << 5;
                    byte = state & 255;
                }
            };
            fill(random.data, size);
            fill(random.resource, size * 2 + 3);
            entries.push_back(std::move(random));
        }
        auto output = encode(entries);
        std::ofstream stream(argv[1], std::ios::binary);
        stream.write(reinterpret_cast<const char*>(output.data()), output.size());
        stream.close();
        if (!stream)
            throw std::runtime_error("Cannot write test archive");
        auto reject = [](const std::vector<Entry>& entries, bool cancel = false) {
            try {
                encode(entries, [=] { return cancel; });
            }
            catch (const std::exception&) {
                return;
            }
            throw std::runtime_error("Invalid archive unexpectedly accepted");
        };
        reject({folder, folder});
        reject({file}); // Missing explicit parent folder.
        auto invalid = file;
        invalid.name = "../escape";
        reject({invalid});
        invalid.name = std::string("bad\0name", 8);
        reject({invalid});
        invalid.name = "\xe4\xb8\xad"; // Not representable in MacRoman.
        reject({invalid});
        invalid.name.clear();
        for (unsigned i = 0; i < 130; ++i)
            invalid.name += "a/";
        invalid.name += "file";
        reject({invalid});
        invalid = folder;
        invalid.data = {1};
        reject({invalid});
        reject({folder, file}, true);
        std::cout << "StuffIt: dual forks, nested/empty folders, MacRoman names, metadata and safe failures passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
