cmake_minimum_required(VERSION 3.16)
project(romaw_images LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(sevenzip third_party/7zip)
set(sources
  ${sevenzip}/CPP/Common/StringToInt.cpp
  ${sevenzip}/CPP/7zip/Common/PropId.cpp
  ${sevenzip}/CPP/7zip/Compress/BitlDecoder.cpp
  ${sevenzip}/CPP/7zip/Archive/Base64Handler.cpp
  ${sevenzip}/CPP/Common/MyString.cpp
  ${sevenzip}/CPP/Common/MyVector.cpp
  ${sevenzip}/CPP/Common/StringConvert.cpp
  ${sevenzip}/CPP/Common/UTFConvert.cpp
  ${sevenzip}/CPP/Common/IntToString.cpp
  ${sevenzip}/CPP/Common/MyWindows.cpp
  ${sevenzip}/CPP/Common/MyXml.cpp
  ${sevenzip}/CPP/Windows/PropVariant.cpp
  ${sevenzip}/CPP/Windows/PropVariantUtils.cpp
  ${sevenzip}/CPP/Windows/TimeUtils.cpp
  ${sevenzip}/CPP/7zip/Common/LimitedStreams.cpp
  ${sevenzip}/CPP/7zip/Common/StreamObjects.cpp
  ${sevenzip}/CPP/7zip/Common/StreamUtils.cpp
  ${sevenzip}/CPP/7zip/Common/InBuffer.cpp
  ${sevenzip}/CPP/7zip/Common/OutBuffer.cpp
  ${sevenzip}/CPP/7zip/Common/ProgressUtils.cpp
  ${sevenzip}/CPP/7zip/Common/CWrappers.cpp
  ${sevenzip}/CPP/7zip/Common/FilterCoder.cpp
  ${sevenzip}/CPP/7zip/Archive/HfsHandler.cpp
  ${sevenzip}/CPP/7zip/Archive/DmgHandler.cpp
  ${sevenzip}/CPP/7zip/Archive/ApmHandler.cpp
  ${sevenzip}/CPP/7zip/Archive/Iso/IsoHandler.cpp
  ${sevenzip}/CPP/7zip/Archive/Iso/IsoHeader.cpp
  ${sevenzip}/CPP/7zip/Archive/Iso/IsoIn.cpp
  ${sevenzip}/CPP/7zip/Archive/Iso/IsoRegister.cpp
  ${sevenzip}/CPP/7zip/Archive/Common/ItemNameUtils.cpp
  ${sevenzip}/CPP/7zip/Archive/HandlerCont.cpp
  ${sevenzip}/CPP/7zip/Archive/Common/FindSignature.cpp
  ${sevenzip}/CPP/7zip/Archive/Common/OutStreamWithCRC.cpp
  ${sevenzip}/CPP/7zip/Compress/ZlibDecoder.cpp
  ${sevenzip}/CPP/7zip/Compress/DeflateDecoder.cpp
  ${sevenzip}/CPP/7zip/Compress/BZip2Decoder.cpp
  ${sevenzip}/CPP/7zip/Compress/BZip2Crc.cpp
  ${sevenzip}/CPP/7zip/Compress/CopyCoder.cpp
  ${sevenzip}/CPP/7zip/Compress/LzfseDecoder.cpp
  ${sevenzip}/CPP/7zip/Compress/XzDecoder.cpp
  ${sevenzip}/CPP/7zip/Compress/LzOutWindow.cpp
  ${sevenzip}/C/7zCrc.c
  ${sevenzip}/C/7zCrcOpt.c
  ${sevenzip}/C/CpuArch.c
  ${sevenzip}/C/Alloc.c
  ${sevenzip}/C/LzmaDec.c
  ${sevenzip}/C/Lzma2Dec.c
  ${sevenzip}/C/Xz.c
  ${sevenzip}/C/XzDec.c
  ${sevenzip}/C/XzIn.c
  ${sevenzip}/C/XzCrc64.c
  ${sevenzip}/C/XzCrc64Opt.c
  ${sevenzip}/C/Sha256.c
  ${sevenzip}/C/Sha256Opt.c
  ${sevenzip}/C/Bra.c
  ${sevenzip}/C/Bra86.c
  ${sevenzip}/C/BraIA64.c
  ${sevenzip}/C/Delta.c
  ${sevenzip}/C/Sort.c
)
add_library(romaw-images STATIC src/images.cpp src/diskcopy.c ${sources})
# These project registration hooks are local additions to the vendored readers.
# Keep vendor sources intact while matching the maintained ROMAW interface.
set_source_files_properties(${sevenzip}/CPP/7zip/Archive/HfsHandler.cpp
  PROPERTIES COMPILE_DEFINITIONS executor_link_hfs=romaw_link_hfs)
set_source_files_properties(${sevenzip}/CPP/7zip/Archive/DmgHandler.cpp
  PROPERTIES COMPILE_DEFINITIONS executor_link_dmg=romaw_link_dmg)
set_source_files_properties(${sevenzip}/CPP/7zip/Archive/ApmHandler.cpp
  PROPERTIES COMPILE_DEFINITIONS executor_link_apm=romaw_link_apm)
set_source_files_properties(${sevenzip}/CPP/7zip/Archive/Iso/IsoRegister.cpp
  PROPERTIES COMPILE_DEFINITIONS executor_link_iso=romaw_link_iso)
file(GLOB hfsutils_sources CONFIGURE_DEPENDS third_party/hfsutils/*.c)
target_sources(romaw-images PRIVATE ${hfsutils_sources} src/hfs_io.cpp src/hfs_metadata.c)
target_compile_definitions(romaw-images PRIVATE HAVE_MKTIME=1)
target_include_directories(romaw-images PUBLIC include PRIVATE ${sevenzip}/CPP ${sevenzip}/C)
target_compile_definitions(romaw-images PRIVATE Z7_ST Z7_EXTRACT_ONLY)
if(WIN32)
  target_link_libraries(romaw-images PUBLIC oleaut32 uuid)
endif()
add_executable(image-probe src/probe.cpp)
add_executable(archive-image-readback ../archives/tests/image_readback.cpp)
target_link_libraries(archive-image-readback PRIVATE romaw-images)
target_link_libraries(image-probe PRIVATE romaw-images)
add_executable(image-write-test tests/direct_write_test.cpp)
target_link_libraries(image-write-test PRIVATE romaw-images)
add_executable(image-ndif-test tests/ndif_image_test.cpp)
target_link_libraries(image-ndif-test PRIVATE romaw-images)
add_executable(image-direct-test tests/direct_catalog_test.cpp)
target_link_libraries(image-direct-test PRIVATE romaw-images)
add_executable(image-projection-test tests/projection_test.cpp)
target_include_directories(image-projection-test PRIVATE ${sevenzip}/CPP ${sevenzip}/C)
target_compile_definitions(image-projection-test PRIVATE Z7_ST Z7_EXTRACT_ONLY)
target_link_libraries(image-projection-test PRIVATE romaw-images)
add_executable(ndif-test tests/ndif_test.cpp ${sevenzip}/CPP/Common/MyWindows.cpp)
target_include_directories(ndif-test PRIVATE ${sevenzip}/CPP)
if(WIN32)
  target_link_libraries(ndif-test PRIVATE oleaut32 uuid)
endif()
enable_testing()
add_test(NAME ndif COMMAND ndif-test)
add_test(NAME projection COMMAND image-projection-test ${CMAKE_CURRENT_BINARY_DIR}/projection-fixture)
if(MINGW)
  target_link_options(image-probe PRIVATE -static)
  target_link_options(image-write-test PRIVATE -static)
  target_link_options(image-ndif-test PRIVATE -static)
  target_link_options(image-direct-test PRIVATE -static)
  target_link_options(image-projection-test PRIVATE -static)
  target_link_options(ndif-test PRIVATE -static)
endif()
