/*+===================================================================
  File:      info.h

  Summary:   Resolve Get Info metadata and validate application memory fields.

  Classes:   Declared: InfoCodes.

  Functions: Interface declarations; see below.

  Origin:    Executor native Finder Get Info presentation.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#ifndef FINDER_INFO_H
#define FINDER_INFO_H
#include "finder/files.h"
#include <optional>

namespace finder {
struct InfoCodes {
    std::wstring type, creator;
};

// Catalog/sidecar codes take precedence. Only plain DOS files use the live FTM.
// Folders have DInfo rather than file type/creator fields and display placeholders.
InfoCodes info_codes(const fs::path& path, const MacFile& metadata, bool directory, bool image);
// Custom Kind and owner are resolved from the creator application, if available.
std::wstring info_kind(const fs::path& path, const MacFile& metadata, bool directory, bool image,
                       const std::wstring& custom = {}, const std::wstring& owner = {});
// The classic fields use whole 1024-byte units. Reject overflow before encoding SIZE.
std::optional<uint32_t> info_memory_bytes(const std::wstring& kilobytes);
}
#endif
