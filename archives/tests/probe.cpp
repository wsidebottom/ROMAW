/*+===================================================================
  File:      probe.cpp

  Summary:   Run the actual archive codecs for independent fork and failure fixtures.

  Classes:   romaw::archives::Item.

  Functions: main.

  Origin:    ROMAW native archive regression harness.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/
#include "romaw_archives.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
namespace fs = std::filesystem;
using namespace romaw::archives;

int main(int argc, char** argv)
{
    try {
        if (argc != 4)
            throw std::runtime_error("Usage: probe decode|zip|macbinary input output");
        std::ifstream source(fs::u8path(argv[2]), std::ios::binary);
        if (!source)
            throw std::runtime_error("Cannot read fixture");
        Bytes raw(std::istreambuf_iterator<char>(source), {});
        auto items = expand(raw);
        if (std::string(argv[1]) == "zip" || std::string(argv[1]) == "macbinary") {
            if (std::string(argv[1]) == "macbinary" && items.size() != 1)
                throw std::runtime_error("MacBinary needs one fixture file");
            auto bytes = std::string(argv[1]) == "zip" ? zip(items) : macbinary(items.front());
            std::ofstream out(fs::u8path(argv[3]), std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            if (!out)
                throw std::runtime_error("Cannot write archive");
        }
        else {
            fs::path root = fs::u8path(argv[3]);
            // expand() validates the entire catalog before any output is created.
            for (const auto& item : items) {
                auto path = root / fs::u8path(item.name);
                fs::create_directories(item.directory ? path : path.parent_path());
                auto write = [&](const fs::path& target, const Bytes& bytes) {
                    std::ofstream out(target, std::ios::binary);
                    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                    if (!out)
                        throw std::runtime_error("Cannot write extracted fixture");
                };
                if (!item.directory)
                    write(path, item.data);
                if (!item.metadata.empty())
                    write(path.parent_path() / ("._" + path.filename().string()), item.metadata);
            }
        }
        std::cout << items.size() << " logical archive items verified\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
