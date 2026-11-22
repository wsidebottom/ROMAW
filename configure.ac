dnl +===================================================================
dnl   File:      configure.ac
dnl
dnl   Summary:   Configures platform and dependency checks for ROMAW build and development support.
dnl
dnl   Classes:   None.
dnl
dnl   Functions: No explicitly exported functions.
dnl
dnl   Origin:    Existing repository source in executor/src; original attribution and provenance comments are preserved
dnl              below.
dnl
dnl   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
dnl ===================================================================+

AC_INIT([ROMAW], m4_esyscmd_s([git rev-parse --short=12 HEAD]))
AC_CANONICAL_BUILD
AC_CANONICAL_HOST
case "$host_cpu-$host_os" in
  x86_64-mingw32*) ;;
  *) AC_MSG_ERROR([ROMAW supports only 64-bit Windows (x86_64-w64-mingw32).]);;
esac
AM_INIT_AUTOMAKE([foreign subdir-objects -Wall])
AC_CONFIG_SRCDIR([runtime/romaw.c])
AC_CONFIG_HEADERS([config.h])

AC_ARG_WITH([front-end], [AS_HELP_STRING([--with-front-end=sdl], [Windows SDL frontend])],
            [AS_IF([test "x$withval" != xsdl], [AC_MSG_ERROR([Only the SDL frontend is supported.])])])
AC_ARG_WITH([sound], [AS_HELP_STRING([--with-sound=sdl], [Windows SDL sound])],
            [AS_IF([test "x$withval" != xsdl], [AC_MSG_ERROR([Only SDL sound is supported.])])])
front_end=sdl
sound_config=sdl
AC_SUBST([front_end])
AC_SUBST([sound_config])
AM_PROG_AS
AC_PROG_CC
AC_PROG_CXX
AM_PROG_CC_C_O
AM_PROG_AR
AC_PROG_RANLIB
AC_CHECK_TOOL([WINDRES], [windres])
AC_PROG_YACC
AC_CHECK_PROG([PERL], [perl], [perl])
AC_ARG_VAR([PERL], [perl for generating blitters])
AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE
AC_CHECK_SIZEOF([char *])
AS_IF([test "$ac_cv_sizeof_char_p" != 8], [AC_MSG_ERROR([A 64-bit Windows compiler is required.])])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#if !defined(_WIN32) || !defined(__x86_64__)
#error Not a Windows x64 compiler
#endif
]], [[]])], [], [AC_MSG_ERROR([A Windows x64 compiler is required.])])
AC_CHECK_LIB([syn68k], [initialize_68k_emulator], [], [AC_MSG_ERROR([Build Windows x64 syn68k first.])])
AC_CHECK_LIB([SDL], [SDL_Init], [], [AC_MSG_ERROR([Windows x64 SDL 1.2 is required.])])
AC_CHECK_LIB([pthread], [pthread_create])
AC_CHECK_LIB([m], [pow])

# WSL generators/tests use a build-only header, not a second ROMAW host port.
AC_CONFIG_LINKS([build-arch-config.h:platform/arch/x86_64/x86_64.h
                 build-os-config.h:tools/romaw/build-host.h
                 host-arch-config.h:platform/arch/x86_64/x86_64.h
                 host-os-config.h:platform/os/mingw32/mingw32.h
                 front-end-config.h:frontend-sdl/sdl.h
                 sound-config.h:sound/sdl/sdl-sound.h])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
