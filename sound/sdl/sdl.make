# +===================================================================
#   File:      sdl.make
#
#   Summary:   Defines build targets and dependencies for host-platform configuration and integration.
#
#   Classes:   None.
#
#   Functions: No explicitly exported functions.
#
#   Origin:    Existing repository source in executor/src/config/sound/sdl; original attribution and provenance comments
#              are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+


SOUND_LIBS = # This should be used with the SDL front end

SOUND_SRC = sdl-sound.c

SOUND_OBJ = $(SOUND_SRC:.c=.o)

clean::
	rm -f $(SOUND_OBJ)
