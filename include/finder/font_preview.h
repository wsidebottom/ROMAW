#pragma once

#include "finder/files.h"

namespace finder {

// Open a modeless preview of a single font resource or of a font family.
bool is_preview_font(uint32_t resource_type);
bool is_font_preview_window(HWND window);
void show_font_preview(HWND desktop, HINSTANCE instance, const std::wstring& identity, const std::wstring& name,
                       const SuitcaseFork& fork, const ResourceKey& key);

}
