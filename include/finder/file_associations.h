/*+===================================================================
  File:      file_associations.h

  Summary:   Register Windows disk-image associations using ROMAW's embedded icons.

  Classes:   None declared.

  Functions: finder::register_disk_image_types.

  Origin:    ROMAW native Finder integration with the Windows registry and shell.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#pragma once
#include <filesystem>
#include <windows.h>

namespace finder {
// An alternate root permits isolated registry fixtures; production uses HKCU.
void register_disk_image_types(const std::filesystem::path& executable = {}, HKEY root = HKEY_CURRENT_USER);
}
