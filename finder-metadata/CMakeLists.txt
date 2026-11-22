# +===================================================================
#   File:      CMakeLists.txt
#
#   Summary:   Build shared Finder metadata and generate its case/icon tables.
#
#   Classes:   None declared.
#
#   Functions: CMake configuration entry point; no named functions.
#
#   Origin:    Existing Finder metadata library CMake configuration.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

cmake_minimum_required(VERSION 3.16)
project(romaw_finder_metadata LANGUAGES C CXX)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)
set(case_generator "${CMAKE_CURRENT_SOURCE_DIR}/../tools/generate-dsstore-case.py")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/metadata_case.inc"
  COMMAND "${Python3_EXECUTABLE}" "${case_generator}"
          "${CMAKE_CURRENT_BINARY_DIR}/generated/metadata_case.inc"
  DEPENDS "${case_generator}")
add_library(romaw-finder-metadata STATIC
  "${CMAKE_CURRENT_BINARY_DIR}/generated/metadata_case.inc"
  src/metadata.cpp
  src/icons.c
  src/iconfiles.cpp
  src/specialicons.c
  ../host/findermetadata.cpp)

file(GLOB shared_icon_inputs CONFIGURE_DEPENDS "../bitmaps/*.icns" "../bitmaps/*.ico")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/finder_folder_icons.inc"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/generate-folder-icons.py"
          --output "${CMAKE_CURRENT_BINARY_DIR}/generated"
  DEPENDS ${shared_icon_inputs} ../bitmaps/mac-palettes.r
          ../tools/generate-folder-icons.py ../tools/mac_icons.py
          ../tools/resource_catalog.py ../tools/rez_types.py)
target_sources(romaw-finder-metadata PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/generated/finder_folder_icons.inc")
target_compile_features(romaw-finder-metadata PRIVATE cxx_std_17)
target_include_directories(romaw-finder-metadata PRIVATE
  include ../include "${CMAKE_CURRENT_BINARY_DIR}"
  "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(romaw-finder-metadata PUBLIC Threads::Threads)
if(WIN32)
  target_link_libraries(romaw-finder-metadata PUBLIC ole32 uuid)
endif()
if(NOT WIN32)
  find_package(Iconv REQUIRED)
  target_link_libraries(romaw-finder-metadata PUBLIC Iconv::Iconv)
endif()

option(FINDER_METADATA_BUILD_TESTS "Build metadata regression checks" OFF)
if(FINDER_METADATA_BUILD_TESTS)
  add_executable(metadata-test tests/metadata_test.cpp
    ../runtime/configprefs.c
    ../runtime/prefspacing.c
    ../runtime/user_profile.cpp)
  target_include_directories(metadata-test PRIVATE include ../include)
  target_compile_features(metadata-test PRIVATE cxx_std_17)
  target_link_libraries(metadata-test PRIVATE romaw-finder-metadata)
  if(MINGW)
    target_link_options(metadata-test PRIVATE -static)
  endif()
endif()
