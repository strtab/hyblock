cmake_minimum_required(VERSION 3.27)
project(hyblock VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET
    hyprland
    pixman-1
    libdrm
    libinput
    libudev
    wayland-server
    xkbcommon
    hyprlang
    hyprutils
    hyprgraphics
    aquamarine
    luajit
)

add_library(hyblock SHARED src/main.cpp)
target_link_libraries(hyblock PRIVATE PkgConfig::deps)
target_compile_definitions(hyblock PRIVATE WLR_USE_UNSTABLE)

# Allows the plugin to be unloaded cleanly (GCC only).
target_compile_options(hyblock PRIVATE $<$<CXX_COMPILER_ID:GNU>:-fno-gnu-unique>)

# Result: $out/lib/libhyblock.so
install(TARGETS hyblock LIBRARY DESTINATION lib)
