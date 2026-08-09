#################################################################################
# toolchain-msvc-clangcl.cmake
#--------------------------------------------------------------------------------
# Cross-compiles this project from Linux to a true MSVC-ABI Windows binary using
# clang-cl (Clang's cl.exe-compatible driver) + lld-link, against an MSVC +
# Windows SDK sysroot obtained with `xwin` (https://github.com/Jake-Shadle/xwin).
# There is no real "cross-compiling MSVC" -- cl.exe itself doesn't run on Linux --
# so this is the standard substitute: same ABI, same calling convention, same
# .lib/.pdb-shaped toolchain, driven by Clang instead of cl.exe.
#
# One-time setup on Arch:
#   sudo pacman -S llvm lld clang
#   cargo install xwin              # or grab a prebuilt binary from its Releases page
#   xwin --accept-license splat --output ~/.xwin
#
# Configure with:
#   cmake -B build/msvc -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=toolchain-msvc-clangcl.cmake \
#     -DXWIN_SYSROOT=$HOME/.xwin
#   cmake --build build/msvc
#
# Ninja is strongly recommended: CMake's Makefile generator assumes a Unix
# compiler driver in a few places that clang-cl trips over.
#################################################################################

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

# --- locate clang-cl / lld-link -------------------------------------------------
find_program(CLANG_CL_EXECUTABLE NAMES clang-cl clang-cl-19 clang-cl-18 clang-cl-17 REQUIRED)
find_program(LLD_LINK_EXECUTABLE NAMES lld-link REQUIRED)
find_program(LLVM_LIB_EXECUTABLE NAMES llvm-lib REQUIRED)
find_program(LLVM_RC_EXECUTABLE  NAMES llvm-rc)

set(CMAKE_C_COMPILER   "${CLANG_CL_EXECUTABLE}")
set(CMAKE_CXX_COMPILER "${CLANG_CL_EXECUTABLE}")
set(CMAKE_LINKER       "${LLD_LINK_EXECUTABLE}")
set(CMAKE_AR           "${LLVM_LIB_EXECUTABLE}")
if(LLVM_RC_EXECUTABLE)
    set(CMAKE_RC_COMPILER "${LLVM_RC_EXECUTABLE}")
endif()

set(CMAKE_C_COMPILER_TARGET   x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)

# Tell CMake explicitly what it's dealing with so its MSVC-abi codepaths
# (import-lib naming, /MT vs /MD selection, etc.) switch on even though the
# compiler binary is called "clang-cl" and we're host-compiling on Linux.
set(CMAKE_CXX_COMPILER_ID "Clang")
set(CMAKE_CXX_COMPILER_FRONTEND_VARIANT "MSVC")
set(CMAKE_C_COMPILER_ID "Clang")
set(CMAKE_C_COMPILER_FRONTEND_VARIANT "MSVC")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# --- xwin sysroot ----------------------------------------------------------------
if(NOT DEFINED XWIN_SYSROOT)
    if(DEFINED ENV{XWIN_SYSROOT})
        set(XWIN_SYSROOT "$ENV{XWIN_SYSROOT}")
    else()
        set(XWIN_SYSROOT "$ENV{HOME}/.xwin")
    endif()
endif()

if(NOT EXISTS "${XWIN_SYSROOT}/crt")
    message(FATAL_ERROR
        "XWIN_SYSROOT (${XWIN_SYSROOT}) doesn't look like an xwin splat output "
        "(expected a crt/ subdirectory). Run:\n"
        "  xwin --accept-license splat --output ${XWIN_SYSROOT}\n"
        "or pass -DXWIN_SYSROOT=/path/to/your/xwin/splat")
endif()

set(_xwin_includes
    "${XWIN_SYSROOT}/crt/include"
    "${XWIN_SYSROOT}/sdk/include/ucrt"
    "${XWIN_SYSROOT}/sdk/include/um"
    "${XWIN_SYSROOT}/sdk/include/shared"
)
set(_xwin_libs
    "${XWIN_SYSROOT}/crt/lib/x86_64"
    "${XWIN_SYSROOT}/sdk/lib/um/x86_64"
    "${XWIN_SYSROOT}/sdk/lib/ucrt/x86_64"
)

foreach(_inc ${_xwin_includes})
    string(APPEND CMAKE_C_FLAGS_INIT   " /imsvc\"${_inc}\"")
    string(APPEND CMAKE_CXX_FLAGS_INIT " /imsvc\"${_inc}\"")
endforeach()

foreach(_lib ${_xwin_libs})
    string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT    " /libpath:\"${_lib}\"")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS_INIT " /libpath:\"${_lib}\"")
    string(APPEND CMAKE_MODULE_LINKER_FLAGS_INIT " /libpath:\"${_lib}\"")
endforeach()

# xwin's Windows/CRT headers are case-correct but Windows filesystems are
# case-insensitive and some vendored headers (imgui, sqlite3, nlohmann_json)
# may #include system headers with inconsistent casing; this keeps clang-cl
# from choking on that the way a case-sensitive Linux filesystem otherwise would.
string(APPEND CMAKE_C_FLAGS_INIT   " -Wno-nonportable-include-path")
string(APPEND CMAKE_CXX_FLAGS_INIT " -Wno-nonportable-include-path")

# Two warning classes that are noisy but not actionable across these
# projects, applied globally so every project sharing this toolchain
# file gets quiet output without per-project flag duplication:
#   -Wno-nontrivial-memcall: imgui's ImVector/ImDrawList etc. use
#     memset/memcpy on non-POD-by-the-letter-of-the-standard structs by
#     design (a well-known, safe pattern upstream); clang-cl flags every
#     call site individually.
#   -Wno-deprecated-declarations + _CRT_SECURE_NO_WARNINGS: silences the
#     MSVC CRT's strncpy/sprintf/etc-are-deprecated nagging. Purely a
#     style opinion of the CRT headers, not a real safety issue here.
string(APPEND CMAKE_C_FLAGS_INIT   " -Wno-nontrivial-memcall -Wno-deprecated-declarations -D_CRT_SECURE_NO_WARNINGS")
string(APPEND CMAKE_CXX_FLAGS_INIT " -Wno-nontrivial-memcall -Wno-deprecated-declarations -D_CRT_SECURE_NO_WARNINGS")

set(CMAKE_FIND_ROOT_PATH "${XWIN_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)