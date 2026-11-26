#pragma once

#include "finder/files.h"

namespace finder {

// Play an owned snd resource, AIFF/AIFF-C file or WAVE file without blocking Finder.
void play_sound_preview(Bytes bytes);
void stop_sound_preview();

}
