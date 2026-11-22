/*+===================================================================
  File:      stuffit.h

  Summary:   Package private staged Macintosh items into verified archives.

  Classes:   None declared; uses CopyJob.

  Functions: Interface declarations; see finder/stuffit.cpp.

  Origin:    ROMAW native Finder integration with the StuffIt C++ bridge.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/
#ifndef FINDER_STUFFIT_H
#define FINDER_STUFFIT_H
#include "finder/files.h"

namespace finder {
void write_macbinary_file(const fs::path& staged, const fs::path& name, const fs::path& output, CopyJob& job);
bool read_macbinary_file(const fs::path& source, ImportedFile& file, bool data);
bool lookup_host_type(const fs::path& path, uint32_t& type, uint32_t& creator, bool& strip);
void write_stuffit_folder(const fs::path& staged, const fs::path& name, const fs::path& output, CopyJob& job,
                          bool zip = false);
}
#endif
