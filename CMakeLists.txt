cmake_minimum_required(VERSION 3.20)
project(gw2-world-events)

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_CXX_STANDARD 17)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_custom_target(bump_rev
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/bump_rev.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Bumping revision..."
    VERBATIM
)

add_library(gw2-world-events SHARED
    src/addon.cpp
    src/addon_options.cpp
    src/maprender.cpp
    src/events.cpp
    src/cyclicrender.cpp
    src/cyclic.cpp
    src/settings.cpp
    include/imgui.cpp
    include/imgui_draw.cpp
    include/imgui_tables.cpp
    include/imgui_widgets.cpp
)

add_dependencies(gw2-world-events bump_rev)

target_include_directories(gw2-world-events PRIVATE include)

set_target_properties(gw2-world-events PROPERTIES
    OUTPUT_NAME "gw2-world-events"
    PREFIX ""
    SUFFIX ".dll"
)

target_compile_options(gw2-world-events PRIVATE
    -Os
    -ffunction-sections
    -fdata-sections
    -fno-rtti
)

target_link_options(gw2-world-events PRIVATE
    -static-libgcc
    -static-libstdc++
    -static
    -s
    -Wl,--gc-sections
    -Wl,--exclude-libs,ALL
)
