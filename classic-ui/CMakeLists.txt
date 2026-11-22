# +===================================================================
#   File:      CMakeLists.txt
#
#   Summary:   Build the shared classic drawing primitives as a static C library with optional standalone tests.
#
#   Classes:   None.
#
#   Functions: CMake build entry points; no exported functions.
#
#   Origin:    Build boundary for the renderer shared by Executor's QuickDraw and native adapters.
#
#   Copyright and Legal notices: Applicable repository license: ../COPYING.
# ===================================================================+

cmake_minimum_required(VERSION 3.16)
project(romaw_classic_ui LANGUAGES C)
add_library(romaw-classic-ui STATIC src/classic_ui.c)
target_compile_features(romaw-classic-ui PUBLIC c_std_11)
target_include_directories(romaw-classic-ui PUBLIC include)

option(ROMAW_CLASSIC_UI_BUILD_TESTS "Build the shared drawing regression fixture" OFF)
if(ROMAW_CLASSIC_UI_BUILD_TESTS)
  enable_testing()
  add_executable(classic-ui-test ../system/tests/classic_ui_test.c)
  target_link_libraries(classic-ui-test PRIVATE romaw-classic-ui)
  add_test(NAME classic-ui COMMAND classic-ui-test)
endif()
