include_guard()
include(FetchContent)

# Official single-header release tarball (recommended FetchContent method as
# of nlohmann/json >= 3.11.3). Provides the nlohmann_json::nlohmann_json
# INTERFACE target; no add_library/target_sources needed here since the
# upstream CMakeLists.txt already does it for us.
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(json)
