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
  src/host/win32/SaveChangesDialog.cpp
  src/host/win32/UpdateDialog.cpp
  src/host/win32/Updater.cpp
  # Joined the exe in phase 46 slice 3, behind the single CreateWindowExW in
  # createControls ([editor] d2d, DEFAULT OFF). This is the first time these two are
  # compiled as part of BUILD_OK — Release /MT with NDEBUG, /W4 /permissive- — rather
  # than only into the EXCLUDE_FROM_ALL demo/test targets below.
  src/host/win32/D2DEditor.cpp
  src/editor/EditorModel.cpp
  # Joined the exe in phase 46 slice 4: the syntax-colouring rules, lifted out of
  # MainWindow.cpp::highlight() so BOTH editors run the same lexer. The RichEdit path turns
  # its spans into EM_SETCHARFORMAT calls; the Direct2D control draws them.
  src/editor/SyntaxLexer.cpp
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
  # Fallback marketing version must track MKT in scripts\build.bat (0.1.1). This is
  # only used for a raw `cmake --build` without build.bat — a degenerate "you forgot
  # the build script" case that yields build 0 — but keep it consistent, not stale.
  file(WRITE "${SENTINEL_GENERATED_DIR}/Version.h"
    "#pragma once\n"
    "#define SENTINEL_BUILD_NUMBER 0\n"
    "#define SENTINEL_VERSION_MARKETING \"0.1.3\"\n"
    "#define SENTINEL_VERSION_DISPLAY \"0.1.3 (build 0)\"\n"
    "#define SENTINEL_VERSION_DISPLAY_W L\"0.1.3 (build 0)\"\n"
    "#define SENTINEL_FILEVERSION 0,1,3,0\n"
    "#define SENTINEL_FILEVERSION_STR \"0.1.3.0\"\n"
    "#define SENTINEL_FILEVERSION_STR_W L\"0.1.3.0\"\n")
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
  comctl32 dwmapi uxtheme gdi32 user32 shell32 ole32 bcrypt cabinet advapi32 wininet
  # D2DEditor.cpp (slice 3): d2d1+dwrite the renderer, imm32 the IME composition window,
  # windowscodecs the offscreen PNG path. windowscodecs is kept even though only the test
  # calls d2dEditorRenderToPng — WindowsCodecs.dll is present on every supported Windows,
  # and #ifdef-ing it out would mean the shipping binary and the tested binary differ in
  # exactly the file this slice is about.
  d2d1 dwrite imm32 windowscodecs)

# Sentinel-native parsers (src/sentinel/parsers.sentinel — the diagnostic + trust
# parsers). scripts\build.bat compiles them to build/generated/parsers.lib via
# `snc build --lib`; when that lib is present the host's parseDiag / loadTrust / readSig / loadProject call
# INTO Sentinel (SENTINELIDE_SENTINEL), otherwise they use the C++ fallbacks. Linking a
# Sentinel static lib additionally requires these Windows system libs (they satisfy the
# Rust runtime the lib pulls in — undocumented upstream, filed as R8 in
# docs/Sentinel-lang_request.md).
set(SENTINEL_PARSERS_LIB "${SENTINEL_GENERATED_DIR}/parsers.lib")
if(EXISTS "${SENTINEL_PARSERS_LIB}")
  target_compile_definitions(SentinelIDE PRIVATE SENTINELIDE_SENTINEL)
  target_link_libraries(SentinelIDE PRIVATE
    "${SENTINEL_PARSERS_LIB}"
    legacy_stdio_definitions ntdll userenv ws2_32 dbghelp)
  message(STATUS "SentinelIDE: diagnostic + trust + sig + manifest parsers = Sentinel (parsers.lib linked)")
else()
  message(STATUS "SentinelIDE: diagnostic + trust + sig + manifest parsers = C++ fallback (parsers.lib absent)")
endif()

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

# Characterization tests for Project.h::saveProject -- the comment-preserving manifest
# WRITER, the last file-touching path still in C++. It had no coverage at all while being
# the one piece of code that rewrites the user's manifest in place; these pin what it does
# today so the planned Sentinel port has an oracle.
#     cmake --build build --target saveproject_test  &&  build\saveproject_test.exe
add_executable(saveproject_test tests/saveproject_test.cpp)
target_include_directories(saveproject_test PRIVATE src/core src)
target_link_libraries(saveproject_test PRIVATE shell32 advapi32)
set_target_properties(saveproject_test PROPERTIES EXCLUDE_FROM_ALL TRUE)
add_test(NAME saveproject COMMAND saveproject_test)

# The pure text model behind the coming Direct2D editor (src/editor/EditorModel.*, vendored
# from SQLTerminal-Win32). No Windows UI, so it is unit-testable before any pixel exists.
# Case 10 is the load-bearing one: examples/crypto.sentinel is a COMMITTED SIGNED file that
# opens by default and that Build auto-saves, so the model must round-trip it byte-for-byte
# or saving would invalidate its .sig. The path is passed in so the test does not depend on
# the working directory.
#     cmake --build build --target editor_model_test  &&  build\editor_model_test.exe
add_executable(editor_model_test tests/editor_model_test.cpp src/editor/EditorModel.cpp)
target_include_directories(editor_model_test PRIVATE src)
set_target_properties(editor_model_test PROPERTIES EXCLUDE_FROM_ALL TRUE)
add_test(NAME editor_model COMMAND editor_model_test "${CMAKE_CURRENT_SOURCE_DIR}/examples/crypto.sentinel")

# syntax_lexer_test — the colouring rules on their own (phase 46 slice 4). They used to be
# an inline loop inside MainWindow.cpp::highlight() with no coverage of any kind; they are
# now src/editor/SyntaxLexer.{h,cpp} and BOTH editors run them, so a change here changes what
# every user sees in both. Pure C++ — no Windows, no Direct2D — so it runs anywhere and fast.
# The load-bearing cases are the ones that CROSS A LINE: a block comment and (per the rules
# as they have always been) an unterminated string. The Direct2D control lexes one line at a
# time from a cached start state, so "per-line == whole-buffer" is not a nicety, it is the
# property that keeps the two editors showing the same file.
#     cmake --build build --target syntax_lexer_test  &&  build\syntax_lexer_test.exe
add_executable(syntax_lexer_test tests/syntax_lexer_test.cpp src/editor/SyntaxLexer.cpp)
target_include_directories(syntax_lexer_test PRIVATE src)
set_target_properties(syntax_lexer_test PROPERTIES EXCLUDE_FROM_ALL TRUE)
add_test(NAME syntax_lexer COMMAND syntax_lexer_test)

# d2d_editor_demo — a standalone WIN32 host for src/host/win32/D2DEditor.cpp, the no-wrap
# Direct2D editor control (phase 46 slice 2). NOT a ctest test: interactively it opens a
# window. It exists because slice 2 deliberately does not touch MainWindow.cpp or the
# SentinelIDE target — the message dialect that lets the host drive this control is slice 3
# — so this is the only way to run the renderer without putting the shipping exe at risk.
#     (from a vcvars64 shell -- see scripts\build.bat -- or the link fails with
#      LNK1181: cannot open input file 'd2d1.lib')
#     cmake --build build --target d2d_editor_demo
#     build\d2d_editor_demo.exe [file]     (default examples\crypto.sentinel, READ-ONLY)
#     build\d2d_editor_demo.exe --render out.png [file]     (headless; exit 0 == wrote it)
#     powershell -File scripts\capture.ps1 -Class SentinelD2DEditorDemo
# capture.ps1 DOES work on this window (verified: dark ground, text, no white) -- but only
# while the window is FOREGROUND. A D2D swap chain that has never been composited has nothing
# for PrintWindow to copy, so a background or minimised window captures BLANK every time, and
# a background process cannot foreground it (SetForegroundWindow is refused unless the caller
# owns the foreground). Re-running does not help; a human has to put the window in front.
# --render below is the better check anyway because it needs no window at all and asserts
# instead of asking you to look.
# NOTE: since slice 3, D2DEditor.cpp is ALSO compiled into the SentinelIDE target above.
# This demo remains the way to drive the control with no IDE around it — in particular it
# has no accelerator table, so onKeyDown's Ctrl+Z / Ctrl+Y arms are reachable here and
# reachable nowhere else (in the exe, TranslateAcceleratorW claims them first and undo goes
# through EM_UNDO/EM_REDO instead).
add_executable(d2d_editor_demo WIN32
  tests/d2d_editor_demo.cpp
  src/host/win32/D2DEditor.cpp
  src/editor/EditorModel.cpp
  src/editor/SyntaxLexer.cpp)
target_include_directories(d2d_editor_demo PRIVATE src)
# d2d1/dwrite: the renderer. imm32: IME composition-window placement. uxtheme+dwmapi:
# Theme.h's SetWindowTheme (the scrollbars are the only non-D2D pixels) and the dark
# title bar. shell32: CommandLineToArgvW. windowscodecs+ole32: the offscreen PNG path.
target_link_libraries(d2d_editor_demo PRIVATE
  d2d1 dwrite imm32 uxtheme dwmapi shell32 windowscodecs ole32)
set_target_properties(d2d_editor_demo PROPERTIES EXCLUDE_FROM_ALL TRUE)

# d2d_render_test — THE pixel test for the Direct2D editor, and the only coverage its
# rendering has. A CONSOLE app (unlike the demo) so ctest can read its output: it creates a
# HIDDEN editor window, renders it offscreen through d2dEditorRenderToPng, decodes the PNG
# back with WIC and asserts on actual pixels — background colour, a floor on the number of
# non-background pixels, proximity to Theme textPrimary, and since phase 46 slice 4 the SYNTAX
# COLOURS: all five Theme text colours present, on the right LINES (the bands are asked of the
# control via EM_LINEINDEX + EM_POSFROMCHAR), plus the painted error-line tint. It fails if the
# control renders a blank window, and it fails if the colouring regresses to monochrome —
# deliberately two separate assertions, because a monochrome editor passes the first.
# capture.ps1 can show you the same thing, but only with a window up and a human looking at
# the PNG; this runs headless in CI and ASSERTS, which is what slices 4-7 need.
# It SKIPS (printing why, exit 0) only when the machine cannot create a D2D/WIC target.
#     cmake --build build --target d2d_render_test  &&  build\d2d_render_test.exe
# The PNG is left at build/d2d_render.png on purpose — look at it.
add_executable(d2d_render_test
  tests/d2d_render_test.cpp
  src/host/win32/D2DEditor.cpp
  src/editor/EditorModel.cpp
  src/editor/SyntaxLexer.cpp)
target_include_directories(d2d_render_test PRIVATE src)
target_link_libraries(d2d_render_test PRIVATE
  d2d1 dwrite imm32 uxtheme dwmapi windowscodecs ole32 advapi32)
set_target_properties(d2d_render_test PROPERTIES EXCLUDE_FROM_ALL TRUE)
add_test(NAME d2d_render COMMAND d2d_render_test
  "${CMAKE_CURRENT_SOURCE_DIR}/examples/crypto.sentinel"
  "${CMAKE_CURRENT_BINARY_DIR}/d2d_render.png")

# d2d_dialect_test — THE test for the RichEdit MESSAGE DIALECT (phase 46 slice 3), i.e. the
# arithmetic the bytes on disk actually pass through. editor_model_test pins
# setText -> textCrlf(); this pins EM_GETTEXTLENGTHEX's return and EM_GETTEXTEX's
# `cb / sizeof(wchar_t)` and `min(size, cap - 1)`, which is where a save gets truncated or
# an editorText() buffer gets overrun. A CONSOLE app (like d2d_render_test) so ctest can read
# its output; it creates a REAL parent window and a REAL WS_CHILD control and drives them with
# the same message shapes MainWindow.cpp sends. The two assertions it exists for: the golden
# byte-identity round trip of examples/crypto.sentinel through saveFile's exact sequence, and
# that EN_CHANGE is SYNCHRONOUS — a regression from SendMessageW to PostMessageW is failure
# mode #1 for this migration and nothing else in the repo can see it. Case 10 (slice 4) adds the
# third: 65 characters typed WITH A REPAINT AFTER EACH must still be ONE undo step, which is the
# whole reason the colouring paints instead of moving the selection.
# It SKIPS (printing why, exit 0) only when the machine has no Direct2D/DirectWrite.
# READ-ONLY on examples/: the golden path is opened GENERIC_READ and there is no write path.
#     cmake --build build --target d2d_dialect_test  &&  build\d2d_dialect_test.exe
add_executable(d2d_dialect_test
  tests/d2d_dialect_test.cpp
  src/host/win32/D2DEditor.cpp
  src/editor/EditorModel.cpp
  src/editor/SyntaxLexer.cpp)
target_include_directories(d2d_dialect_test PRIVATE src)
target_link_libraries(d2d_dialect_test PRIVATE
  d2d1 dwrite imm32 uxtheme dwmapi windowscodecs ole32 advapi32)
set_target_properties(d2d_dialect_test PROPERTIES EXCLUDE_FROM_ALL TRUE)
add_test(NAME d2d_dialect COMMAND d2d_dialect_test
  "${CMAKE_CURRENT_SOURCE_DIR}/examples/crypto.sentinel")

# diag_xcheck / trust_xcheck — prove the Sentinel parsers stay byte-identical to the
# C++ oracles they replaced. Only defined when the Sentinel lib was built; they link
# the same parsers.lib + system libs the app does.
#     cmake --build build --target diag_xcheck  &&  build\diag_xcheck.exe
#     cmake --build build --target trust_xcheck &&  build\trust_xcheck.exe
if(EXISTS "${SENTINEL_PARSERS_LIB}")
  foreach(_xc diag_xcheck trust_xcheck sig_xcheck manifest_xcheck saveproject_xcheck)
    add_executable(${_xc} tests/${_xc}.cpp)
    target_link_libraries(${_xc} PRIVATE
      "${SENTINEL_PARSERS_LIB}"
      legacy_stdio_definitions ntdll userenv ws2_32 dbghelp)
    set_target_properties(${_xc} PROPERTIES EXCLUDE_FROM_ALL TRUE)
  endforeach()
  target_include_directories(manifest_xcheck PRIVATE src/core)  # needs Project.h (C++ oracle)
  target_include_directories(saveproject_xcheck PRIVATE src/core)  # needs Project.h (C++ oracle) + encodeSaveModel
  target_link_libraries(saveproject_xcheck PRIVATE shell32 advapi32)
  add_test(NAME diag_parity  COMMAND diag_xcheck)
  add_test(NAME trust_parity COMMAND trust_xcheck)
  add_test(NAME sig_parity      COMMAND sig_xcheck)
  add_test(NAME manifest_parity COMMAND manifest_xcheck)
  add_test(NAME saveproject_parity COMMAND saveproject_xcheck)
endif()
