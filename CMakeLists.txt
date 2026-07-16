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
    src/addon/addon_options_helpers.cpp
    src/addon/addon_options.cpp
    src/addon/addon.cpp
    src/core/background_threads.cpp
    src/core/notify_sound.cpp
    src/core/settings.cpp
    src/core/subscriptions.cpp
    src/core/subscriptions_cache.cpp
    src/events/events_basic.cpp
    src/events/events_categories.cpp
    src/events/events_cyclic.cpp
    src/events/events_storage.cpp
    src/events/events_tracking.cpp
    src/integration/apikey_crypto.cpp
    src/integration/gw2_api.cpp
    src/integration/weekly_vault.cpp
    src/overlay/maprender.cpp
    src/ui/cyclicrender.cpp
    src/ui/icon_whitener.cpp
    src/ui/subscriptions_bar.cpp
    src/ui/subscriptions_notification.cpp
    src/ui/subscriptions_window.cpp
    include/imgui.cpp
    include/imgui_draw.cpp
    include/imgui_tables.cpp
    include/imgui_widgets.cpp
)

add_dependencies(gw2-world-events bump_rev)

target_include_directories(gw2-world-events PRIVATE
    include
    src/addon
    src/core
    src/events
    src/integration
    src/overlay
    src/ui
)

target_link_libraries(gw2-world-events PRIVATE windowscodecs ole32 winhttp bcrypt winmm)

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