#pragma once

#include "finder/files.h"
#include "finder_metadata/icons.h"

namespace finder {

bool save_application_icon(const MacFile& application, const fs::path& icon);
bool save_application_icon(const FinderIcon& family, const fs::path& icon);
bool save_native_application_icon(const fs::path& application, const fs::path& icon);

}
