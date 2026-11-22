cmake_minimum_required(VERSION 3.16)
project(LC475VGA C)
add_application(VGAStartup main.c startup.r CREATOR "Vg60")
target_compile_options(VGAStartup PRIVATE -Os -m68020 -msoft-float
    -Wall -Wextra -Werror -ffunction-sections -fdata-sections)
target_link_options(VGAStartup PRIVATE -Wl,-gc-sections)

# Both targets use classic 68K Toolbox calls. System 7.1 needs the separately
# installed Display Manager extension; System 7.6 supplies it in the OS.
add_application(VGAStartup71 main.c startup.r CREATOR "Vg60")
target_compile_options(VGAStartup71 PRIVATE -Os -m68020 -msoft-float
    -Wall -Wextra -Werror -ffunction-sections -fdata-sections)
target_link_options(VGAStartup71 PRIVATE -Wl,-gc-sections)

option(VGA_BUILD_TESTS "Build synthetic mode/PRAM tests (no real video writes)" OFF)
if(VGA_BUILD_TESTS)
    add_application(VGAStartupTests tests.c startup.r CREATOR "Vg60")
    target_compile_options(VGAStartupTests PRIVATE -Os -m68020 -msoft-float
        -Wall -Wextra -Werror -ffunction-sections -fdata-sections)
    target_link_options(VGAStartupTests PRIVATE -Wl,-gc-sections)
endif()
