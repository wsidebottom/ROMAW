/*+===================================================================
  File:      romaw_stuffit.hpp

  Summary:   Owned C++ interface to the StuffIt 5 archive encoder.

  Classes:   Entry.

  Functions: Interface declarations; see archive.cpp.

  Origin:    ROMAW adapter for the stuffit-rs versioned C ABI.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/
#ifndef ROMAW_STUFFIT_HPP
#define ROMAW_STUFFIT_HPP
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace romaw::stuffit {
using Bytes = std::vector<unsigned char>;

struct Entry {
    std::string name; // UTF-8, '/' separates explicit parent folder entries.
    Bytes data, resource;
    std::array<unsigned char, 4> type{}, creator{};
    uint32_t created = 0, modified = 0; // Mac epoch; zero means unset.
    uint16_t flags = 0;
    bool folder = false;
};

// Method 13 emits classic StuffIt 5 compression. Throws on invalid names,
// unsupported encodings, cancellation, or failure of complete read-back.
// The caller owns staging/publication and must retain move sources on failure.
Bytes encode(const std::vector<Entry>& entries, const std::function<bool()>& cancelled = {});
}
#endif
