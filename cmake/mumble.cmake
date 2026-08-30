include_guard()
include(FetchContent)

FetchContent_Declare(
    mumble
    GIT_REPOSITORY https://github.com/RaidcoreGG/RCGG-lib-mumble-api.git
    GIT_TAG 8c93788c3042dd6b401c0a93d17e34e1d4414e9a
)

FetchContent_MakeAvailable(mumble)

add_library(mumble INTERFACE)
target_sources(mumble PRIVATE "${mumble_SOURCE_DIR}/Mumble.h")
target_include_directories(mumble INTERFACE ${mumble_SOURCE_DIR})
