cmake_minimum_required(VERSION 3.20)

# Compiler/target selection now lives in the toolchain file rather than being
# hardcoded here, so the same CMakeLists.txt builds under either backend:
#   MinGW (default, unchanged, no -DCMAKE_TOOLCHAIN_FILE needed):
#     cmake -B build/mingw
#   MSVC-ABI via clang-cl + lld-link (xwin sysroot):
#     cmake -B build/msvc -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-msvc-clangcl.cmake -DXWIN_SYSROOT=~/.xwin
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_SYSTEM_NAME Windows)
    set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
    set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
endif()

project(gw2-world-events)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Third-party dependencies, fetched at configure time instead of being
# vendored under include/. See cmake/*.cmake for pinned versions/commits.
# Each build tree (build/mingw, build/msvc) gets its own _deps/ -- do NOT
# point FETCHCONTENT_BASE_DIR at a shared location across them. FetchContent's
# "already populated" bookkeeping is per-build-tree, so the second backend to
# configure re-runs its clone step regardless, and that step's scratch
# project is configured with -G${CMAKE_GENERATOR} (mingw's default Makefiles
# vs msvc's Ninja) -- a shared dir means it collides with the other backend's
# leftover cache there. nlohmann/json's own CMakeLists.txt also gets
# add_subdirectory()'d in (for the mumble/nexus/imgui compiler too), which
# would hit the same kind of conflict from mingw-g++ vs clang-cl once past
# the generator issue. Not worth it for a few small repos.
include(${CMAKE_SOURCE_DIR}/cmake/imgui.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/mumble.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/nexus.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/nlohmann_json.cmake)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_custom_target(check_weekly_vault
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_weekly_vault.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Validating weekly_vault.cpp against events_cyclic.cpp..."
    VERBATIM
)

add_custom_target(bump_rev
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/bump_rev.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Bumping revision..."
    BYPRODUCTS
        ${CMAKE_SOURCE_DIR}/src/addon/version.h
        ${CMAKE_SOURCE_DIR}/src/addon/build_info.h
    VERBATIM
)
add_dependencies(bump_rev check_weekly_vault)

# Regenerates src/generated/bundled_icons.generated.cpp from every PNG under
# resources/textures/ (see tools/generate_bundled_icons.py / src/core/
# bundled_icons.h). CONFIGURE_DEPENDS makes CMake re-glob on the next build
# after a texture is added or removed, not just when an existing one changes.
file(GLOB BUNDLED_ICON_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/resources/textures/*.png)

set(BUNDLED_ICONS_GENERATED ${CMAKE_SOURCE_DIR}/src/generated/bundled_icons.generated.cpp)

add_custom_command(
    OUTPUT ${BUNDLED_ICONS_GENERATED}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/generate_bundled_icons.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/generate_bundled_icons.py ${BUNDLED_ICON_SOURCES}
    COMMENT "Embedding resources/textures/*.png into bundled_icons.generated.cpp..."
    VERBATIM
)

add_library(gw2-world-events SHARED
    src/addon/addon.cpp
    src/core/background_threads.cpp
    src/core/notify_sound.cpp
    src/core/settings.cpp
    src/core/subscriptions.cpp
    src/core/subscriptions_cache.cpp
    src/core/time_format.cpp
    src/events/events_basic.cpp
    src/events/events_categories.cpp
    src/events/events_cyclic.cpp
    src/events/events_live.cpp
    src/events/events_storage.cpp
    src/events/events_tracking.cpp
    src/generated/bundled_icons.generated.cpp
    src/integration/apikey_crypto.cpp
    src/integration/better_chat.cpp
    src/integration/gw2_api.cpp
    src/integration/weekly_vault.cpp
    src/networking/shard_id.cpp
    src/networking/ws_client.cpp
    src/networking/ws_debug_log.cpp
    src/overlay/cyclicrender.cpp
    src/overlay/maprender.cpp
    src/overlay/map_shared.cpp
    src/ui/addon_options_helpers.cpp
    src/ui/addon_options.cpp
    src/ui/changelog_window.cpp
    src/ui/icon_whitener.cpp
    src/ui/live_events_ui.cpp
    src/ui/reset_defaults.cpp
    src/ui/subscriptions_bar.cpp
    src/ui/subscriptions_edit_window.cpp
    src/ui/subscriptions_notification.cpp
    src/ui/subscriptions_window.cpp
    src/ui/ws_debug_window.cpp
)

add_dependencies(gw2-world-events bump_rev)

target_include_directories(gw2-world-events PRIVATE
    src/addon
    src/core
    src/events
    src/integration
    src/networking
    src/overlay
    src/ui
)

# NOMINMAX: without this, <windows.h>/minwindef.h's macro-style min/max
# collide with std::min/std::max call sites (e.g. subscriptions_bar.cpp,
# subscriptions_notification.cpp, icon_whitener.cpp), producing
# "expected unqualified-id" errors under clang-cl/xwin's SDK headers.
target_compile_definitions(gw2-world-events PRIVATE NOMINMAX)

target_link_libraries(gw2-world-events PRIVATE
    windowscodecs ole32 winhttp bcrypt winmm
    imgui
    mumble
    nexus
    nlohmann_json::nlohmann_json
)

set_target_properties(gw2-world-events PROPERTIES
    OUTPUT_NAME "WorldEvents"
    PREFIX ""
    SUFFIX ".dll"
)

if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    # clang-cl / lld-link (MSVC ABI) path.
    # /EHsc: clang-cl disables C++ exceptions by default like cl.exe does.
    # /GR-:  matches -fno-rtti (RTTI stays off).
    # /MT:   static multithreaded CRT -- the MSVC-world analogue of the
    #        mingw -static-libgcc/-static-libstdc++/-static trio, so the
    #        addon DLL doesn't depend on the user having the right VC++
    #        redistributable installed.
    # /O1:   optimize for size, matching -Os.
    target_compile_options(gw2-world-events PRIVATE
        /O1
        /GR-
        /EHsc
        /MT
    )
    target_link_options(gw2-world-events PRIVATE
        /OPT:REF   # dead code/data stripping, ~= --gc-sections
        /OPT:ICF   # identical comdat folding, extra win lld-link gives for free
    )
    # imgui's .cpp files used to be compiled straight into gw2-world-events
    # and inherited these flags for free. Now that imgui is its own fetched
    # static-lib target, it needs them applied explicitly too -- /MT in
    # particular, since linking a /MD-built static lib into a /MT target is
    # a CRT mismatch clang-cl/lld-link will reject at link time.
    target_compile_options(imgui PRIVATE /O1 /GR- /EHsc /MT)
else()
    # mingw-w64 (GNU ABI) path -- unchanged from before.
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
    # See the MSVC branch above -- imgui is now a separate fetched target
    # and no longer inherits these automatically.
    target_compile_options(imgui PRIVATE -Os -ffunction-sections -fdata-sections -fno-rtti)
endif()