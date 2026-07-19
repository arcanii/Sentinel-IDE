# SPDX-License-Identifier: GPL-3.0-or-later
# SentinelIDE — native Windows IDE for the Sentinel language (built in Sentinel).
# Win32 + Common Controls v6, MSVC/Ninja, C++17. Modeled on SQLTerminal-Win32.
cmake_minimum_required(VERSION 3.21)
project(SentinelIDE LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

if(MSVC)
  add_compile_options(/W4 /permissive- /EHsc /utf-8)
  add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# Layered: src/core/ = portable-intended logic (the reuse layer, trending toward a
# Sentinel C-ABI lib); src/host/win32/ = the thin Win32 native host. A macOS/Linux
# port would add src/host/<os>/ against the same core — added when a port starts.
add_executable(SentinelIDE WIN32
  src/host/win32/WinMain.cpp
  src/host/win32/MainWindow.cpp
  src/host/win32/SettingsDialog.cpp
  src/host/win32/ProjectSettingsDialog.cpp
  src/host/win32/SigningDialog.cpp
  src/host/win32/AboutDialog.cpp
  src/host/win32/PasswordDialog.cpp
  src/host/win32/Updater.cpp
  packaging/SentinelIDE.rc)

# WinSparkle (auto-update) — vendored prebuilt DLL/lib/header, 0.9.3 official release
# binaries, x64 only (the app is x64-only, so there is no arch-selection problem to
# solve). Update checks read an EdDSA-signed appcast; see src/host/win32/Updater.cpp
# and docs/RELEASING.md. OFF produces a build with the updater compiled out entirely.
option(SENTINELIDE_UPDATER "Build with WinSparkle auto-update" ON)
if(SENTINELIDE_UPDATER)
  set(WS_DIR "${CMAKE_SOURCE_DIR}/third_party/winsparkle")
  if(NOT EXISTS "${WS_DIR}/lib/x64/WinSparkle.lib")
    message(FATAL_ERROR
      "SENTINELIDE_UPDATER=ON but third_party/winsparkle is missing. "
      "Re-run with -DSENTINELIDE_UPDATER=OFF to build without auto-update.")
  endif()
  add_library(WinSparkle SHARED IMPORTED)
  set_target_properties(WinSparkle PROPERTIES
    IMPORTED_IMPLIB "${WS_DIR}/lib/x64/WinSparkle.lib"
    IMPORTED_LOCATION "${WS_DIR}/bin/x64/WinSparkle.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${WS_DIR}/include")
  target_compile_definitions(SentinelIDE PRIVATE SENTINELIDE_HAVE_WINSPARKLE)
  target_link_libraries(SentinelIDE PRIVATE WinSparkle)
  # WinSparkle.dll must sit beside the exe — it is loaded at process start, so a
  # missing copy is a launch failure, not a lazy one.
  add_custom_command(TARGET SentinelIDE POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WS_DIR}/bin/x64/WinSparkle.dll" "$<TARGET_FILE_DIR:SentinelIDE>"
    COMMENT "Copying WinSparkle.dll next to Sentinel-IDE.exe")
endif()

# Version header — normally written by scripts\build.bat (auto-incrementing build
# number). Provide a fallback so a raw `cmake --build` (no build.bat) still compiles.
set(SENTINEL_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${SENTINEL_GENERATED_DIR}")
if(NOT EXISTS "${SENTINEL_GENERATED_DIR}/Version.h")
  file(WRITE "${SENTINEL_GENERATED_DIR}/Version.h"
    "#pragma once\n"
    "#define SENTINEL_BUILD_NUMBER 0\n"
    "#define SENTINEL_VERSION_MARKETING \"0.1.0\"\n"
    "#define SENTINEL_VERSION_DISPLAY \"0.1.0 (build 0)\"\n"
    "#define SENTINEL_VERSION_DISPLAY_W L\"0.1.0 (build 0)\"\n"
    "#define SENTINEL_FILEVERSION 0,1,0,0\n"
    "#define SENTINEL_FILEVERSION_STR \"0.1.0.0\"\n"
    "#define SENTINEL_FILEVERSION_STR_W L\"0.1.0.0\"\n")
endif()
if(NOT EXISTS "${SENTINEL_GENERATED_DIR}/Loc.h")
  file(WRITE "${SENTINEL_GENERATED_DIR}/Loc.h"
    "#pragma once\n"
    "#define SENTINEL_LOC_CPP 0\n#define SENTINEL_LOC_SENTINEL 0\n"
    "#define SENTINEL_LOC_BUILD 0\n#define SENTINEL_LOC_TOTAL 0\n"
    "#define SENTINEL_LOC_BY_SENTINEL 0\n")
endif()

# The product/exe is "Sentinel-IDE" (matching Sentinel-lang / Sentinel-learning);
# the CMake target id + window class + settings dir stay "SentinelIDE" internally.
set_target_properties(SentinelIDE PROPERTIES OUTPUT_NAME "Sentinel-IDE")

target_include_directories(SentinelIDE PRIVATE src "${SENTINEL_GENERATED_DIR}")
target_link_libraries(SentinelIDE PRIVATE
  comctl32 dwmapi uxtheme gdi32 user32 shell32 ole32 bcrypt cabinet advapi32)

# The .rc embeds app.manifest; stop the linker generating a competing one.
if(MSVC)
  target_link_options(SentinelIDE PRIVATE /MANIFEST:NO)
endif()

# ---- tests ------------------------------------------------------------------
# Console test for the .sealed container format (src/core/Seal.h). Not built by
# scripts\build.bat — build it explicitly:
#     cmake --build build --target seal_test  &&  build\seal_test.exe
# or via ctest:  ctest --test-dir build
enable_testing()
add_executable(seal_test tests/seal_test.cpp)
target_include_directories(seal_test PRIVATE src/core)
target_link_libraries(seal_test PRIVATE bcrypt cabinet shell32 advapi32)
set_target_properties(seal_test PROPERTIES EXCLUDE_FROM_ALL TRUE)
add_test(NAME seal_format COMMAND seal_test)
