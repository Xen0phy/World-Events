include_guard()
include(FetchContent)

# Pinned to main HEAD (2025-08-07, "C compatibility + cleanup") rather than
# the v6.1 tag: that rewrite introduced the _t-suffixed API this project's
# addon.h/addon.cpp/maprender.h/better_chat.cpp are written against
# (AddonAPI_t, NexusLinkData_t, Texture_t, GUI_Register, DataLink_Get,
# Paths_GetAddonDirectory, etc.) -- v6.1 predates it and won't compile
# against this codebase. No tag has been cut since; re-check upstream
# periodically and re-pin to a tag once one exists past this commit.
FetchContent_Declare(
    nexus
    GIT_REPOSITORY https://github.com/RaidcoreGG/RCGG-lib-nexus-api
    GIT_TAG 9b2c53df86c00db6495642bfcff2d0611bd957ef
)

FetchContent_MakeAvailable(nexus)

add_library(nexus INTERFACE)
target_sources(nexus PRIVATE "${nexus_SOURCE_DIR}/Nexus.h")
target_include_directories(nexus INTERFACE ${nexus_SOURCE_DIR})
