/*+===================================================================
  File:      romaw_archives.hpp

  Summary:   Bounded ZIP, StuffIt, BinHex and MacBinary codecs preserving AppleDouble.

  Classes:   Item.

  Functions: Interface declarations; see archives.cpp.

  Origin:    ROMAW native archive services; miniz and stuffit-rs provide compression.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/
#ifndef ROMAW_ARCHIVES_HPP
#define ROMAW_ARCHIVES_HPP
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace romaw::archives {
using Bytes = std::vector<unsigned char>;

struct Item {
    std::string name; // Relative UTF-8 path.
    bool directory = false;
    Bytes data, metadata; // Complete AppleDouble, including resource fork.
};

constexpr size_t byte_limit = 256 * 1024 * 1024;
// Validate every entry before callers create any filesystem output.
void validate(const std::vector<Item>& items);
std::vector<Item> canonicalize(std::vector<Item> osx_layout);
Bytes metadata(const Bytes& finder_info, const Bytes& resource);
Bytes metadata_part(const Bytes& bytes, uint32_t id);
Bytes macbinary(const Item& item, const std::function<bool()>& cancelled = {});
Bytes zip(const std::vector<Item>& items, const std::function<bool()>& cancelled = {});
std::vector<Item> expand(const Bytes& bytes, const std::function<bool()>& cancelled = {});
}
#endif
