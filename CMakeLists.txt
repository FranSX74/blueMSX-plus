# blueMSX+ for Linux (and other POSIX systems).
#
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
#   ./build/bin/qbluemsx   (or bluemsx-imgui, bluemsx-sdl)
#
# Layout:
#   bluemsx-core      the emulator (Src/* except the Windows UI); its file list
#                     is read from the Visual Studio project, so both builds
#                     stay in step.
#   bluemsx-platform  arch* implementation on SDL3 + POSIX (Src/Platform).
#   frontends         qbluemsx (QblueMSX, Qt6), bluemsx-imgui (blueMSX-ImGui),
#                     bluemsx-sdl (minimal).
#
# Windows keeps building with Make/msvc20xx/blueMSX.sln.

cmake_minimum_required(VERSION 3.21)
project(bluemsx-plus VERSION 3.1.1 LANGUAGES C CXX)

include(GNUInstallDirs)

option(BLUEMSX_FRONTEND_IMGUI "Build the Dear ImGui frontend (bluemsx-imgui)" ON)
option(BLUEMSX_FRONTEND_SDL "Build the minimal SDL3 frontend (bluemsx-sdl)" ON)
option(BLUEMSX_FRONTEND_QT "Build the Qt6 frontend (qbluemsx)" ON)
option(BLUEMSX_SANITIZE "Build with AddressSanitizer and UBSan" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(SDL3 REQUIRED CONFIG)

set(BMX ${CMAKE_SOURCE_DIR}/blueMSX)
set(BMX_DATADIR ${CMAKE_INSTALL_FULL_DATADIR}/bluemsx)

if(BLUEMSX_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# ---- Core sources, from the Visual Studio project ----------------------------

set(BMX_VCXPROJ ${BMX}/Make/msvc2026/blueMSX.vcxproj)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${BMX_VCXPROJ})
file(STRINGS ${BMX_VCXPROJ} _lines REGEX "<ClCompile Include=")

set(BMX_CORE_SOURCES)
foreach(_line IN LISTS _lines)
    string(REGEX REPLACE ".*Include=\"([^\"]+)\".*" "\\1" _src "${_line}")
    string(REPLACE "\\" "/" _src "${_src}")
    string(REGEX REPLACE "^\\.\\./\\.\\./" "" _src "${_src}")
    # The Windows user interface, its skin engine and Windows-only devices.
    if(_src MATCHES "^Src/(Win32|Theme)/" OR _src MATCHES "(msxgr\\.cpp|iowin32\\.c)$")
        if(_src MATCHES "^Src/Theme/")
            list(APPEND BMX_THEME_SOURCES ${BMX}/${_src})
        endif()
        continue()
    endif()
    if(NOT EXISTS ${BMX}/${_src})
        # Windows file names are case-insensitive; find the real spelling.
        get_filename_component(_dir ${_src} DIRECTORY)
        get_filename_component(_name ${_src} NAME)
        string(TOLOWER "${_name}" _lname)
        file(GLOB _cands RELATIVE ${BMX}/${_dir} ${BMX}/${_dir}/*)
        set(_found "")
        foreach(_c IN LISTS _cands)
            string(TOLOWER "${_c}" _lc)
            if(_lc STREQUAL _lname)
                set(_found ${_dir}/${_c})
            endif()
        endforeach()
        if(NOT _found)
            message(WARNING "Listed in the VS project but missing: ${_src}")
            continue()
        endif()
        set(_src ${_found})
    endif()
    list(APPEND BMX_CORE_SOURCES ${BMX}/${_src})
endforeach()
list(REMOVE_DUPLICATES BMX_CORE_SOURCES)
list(LENGTH BMX_CORE_SOURCES _n)
message(STATUS "blueMSX+ core: ${_n} source files")

set(BMX_CORE_INCLUDES)
foreach(_d Arch Bios Board BuildInfo Common Debugger Emulator Input IoDevice Language
           Media Memory Resources SoundChips SoundChips/Emu2413 SoundChips/Emu8950
           SoundChips/NukedOPLL SoundChips/OpenMsxY8950Latest Theme TinyXML Unzip Utils
           VideoChips VideoRender Z80)
    list(APPEND BMX_CORE_INCLUDES ${BMX}/Src/${_d})
endforeach()

set(BMX_COMMON_DEFS LSB_FIRST NO_ASM)
set(BMX_COMPAT_HEADER ${BMX}/Src/Platform/PlatformCompat.h)

add_library(bluemsx-core STATIC ${BMX_CORE_SOURCES})
target_include_directories(bluemsx-core PUBLIC ${BMX_CORE_INCLUDES})
target_compile_definitions(bluemsx-core PUBLIC ${BMX_COMMON_DEFS})
target_compile_options(bluemsx-core PUBLIC "SHELL:-include ${BMX_COMPAT_HEADER}")
# Legacy code: quiet, and GCC 14+ needs -fpermissive for its function pointer
# casts in C.
target_compile_options(bluemsx-core PRIVATE -w
    $<$<COMPILE_LANG_AND_ID:C,GNU>:-fpermissive>
    $<$<COMPILE_LANG_AND_ID:C,Clang>:-Wno-incompatible-pointer-types -Wno-int-conversion -Wno-implicit-function-declaration>
    $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-Wno-c++11-narrowing>
    $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
target_link_libraries(bluemsx-core PUBLIC m)

# The skin engine, for the frontends that draw the classic blueMSX window (the
# bitmap and text primitives it calls are theirs to implement).
list(REMOVE_DUPLICATES BMX_THEME_SOURCES)
add_library(bluemsx-theme STATIC ${BMX_THEME_SOURCES} ${BMX}/Src/Frontend/Common/ThemeClassic.c)
target_include_directories(bluemsx-theme PUBLIC ${BMX}/Src/Frontend/Common ${BMX}/Src/Platform ${BMX}/Src/Platform/compat)
target_link_libraries(bluemsx-theme PUBLIC bluemsx-core)
target_compile_options(bluemsx-theme PRIVATE -w
    $<$<COMPILE_LANG_AND_ID:C,GNU>:-fpermissive>
    $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)

# ---- Platform layer ----------------------------------------------------------

add_library(bluemsx-platform OBJECT
    ${BMX}/Src/Platform/Platform.c
    ${BMX}/Src/Platform/PlatformInput.c
    ${BMX}/Src/Platform/PlatformMidi.c
    ${BMX}/Src/Platform/PlatformPaths.c
    ${BMX}/Src/Platform/PlatformRecorder.cpp
    ${BMX}/Src/Platform/PlatformShortcuts.c
    ${BMX}/Src/Platform/PlatformSound.c
    ${BMX}/Src/Platform/PlatformStubs.c
    ${BMX}/Src/Platform/PlatformSystem.c
    ${BMX}/Src/Platform/PlatformUi.c)
target_include_directories(bluemsx-platform PUBLIC ${BMX}/Src/Platform)
target_compile_definitions(bluemsx-platform PRIVATE BLUEMSX_INSTALL_DATADIR="${BMX_DATADIR}")
target_compile_options(bluemsx-platform PRIVATE -Wall -Wno-unused-parameter -Wno-missing-braces)
target_link_libraries(bluemsx-platform PUBLIC bluemsx-core SDL3::SDL3 Threads::Threads)
find_package(Threads REQUIRED)

# Video recording (File > Record video) with the FFmpeg libraries; without
# them the build simply has no recording.
option(BLUEMSX_FFMPEG "Record video with the FFmpeg libraries" ON)
if(BLUEMSX_FFMPEG)
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(FFMPEG IMPORTED_TARGET libavcodec libavformat libavutil libswscale libswresample)
    endif()
endif()
if(FFMPEG_FOUND)
    message(STATUS "Video recording: FFmpeg ${FFMPEG_libavcodec_VERSION}")
    target_compile_definitions(bluemsx-platform PRIVATE BLUEMSX_HAVE_FFMPEG)
    target_link_libraries(bluemsx-platform PUBLIC PkgConfig::FFMPEG)
else()
    message(STATUS "Video recording: off (FFmpeg libraries not found)")
endif()

# MIDI in/out through the ALSA sequencer (Linux; FreeBSD has alsa-lib too).
option(BLUEMSX_ALSA "MIDI with the ALSA sequencer" ON)
if(BLUEMSX_ALSA)
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(ALSA IMPORTED_TARGET alsa)
    endif()
endif()
if(ALSA_FOUND)
    message(STATUS "MIDI: ALSA ${ALSA_VERSION}")
    target_compile_definitions(bluemsx-platform PRIVATE BLUEMSX_HAVE_ALSA)
    target_link_libraries(bluemsx-platform PUBLIC PkgConfig::ALSA)
else()
    message(STATUS "MIDI: off (ALSA not found)")
endif()

# A frontend executable: its own sources + the platform objects + the core.
function(bluemsx_frontend target)
    add_executable(${target} ${ARGN} $<TARGET_OBJECTS:bluemsx-platform>)
    target_link_libraries(${target} PRIVATE bluemsx-platform bluemsx-core SDL3::SDL3)
    target_compile_options(${target} PRIVATE -Wall -Wno-unused-parameter)
    target_include_directories(${target} PRIVATE ${BMX}/Src/Frontend/Common)
    install(TARGETS ${target} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endfunction()

# Toolkit-neutral pieces the full frontends share (menu contents).
set(BMX_FRONTEND_COMMON ${BMX}/Src/Frontend/Common/FrontendMenus.cpp
    ${BMX}/Src/Frontend/Common/DebuggerCore.cpp)

if(BLUEMSX_FRONTEND_SDL)
    bluemsx_frontend(bluemsx-sdl ${BMX}/Src/Frontend/Sdl/SdlMain.c)
endif()

if(BLUEMSX_FRONTEND_IMGUI)
    # Dear ImGui is vendored (third_party/imgui, MIT) with its SDL3 backends.
    set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
    add_library(imgui STATIC
        ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp ${IMGUI_DIR}/imgui_demo.cpp
        ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
    target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
    target_link_libraries(imgui PUBLIC SDL3::SDL3)

    set(BMX_IMGUI ${BMX}/Src/Frontend/ImGui)
    bluemsx_frontend(bluemsx-imgui ${BMX_FRONTEND_COMMON}
        ${BMX_IMGUI}/ImGuiMain.cpp ${BMX_IMGUI}/ImGuiMenus.cpp ${BMX_IMGUI}/ImGuiDialogs.cpp
        ${BMX_IMGUI}/ImGuiFileDialog.cpp ${BMX_IMGUI}/ImGuiProperties.cpp ${BMX_IMGUI}/ImGuiShortcuts.cpp
        ${BMX_IMGUI}/ImGuiDebugger.cpp)
    target_link_libraries(bluemsx-imgui PRIVATE imgui)
endif()

if(BLUEMSX_FRONTEND_QT)
    find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)
    set(BMX_QT ${BMX}/Src/Frontend/Qt)
    bluemsx_frontend(qbluemsx ${BMX_FRONTEND_COMMON}
        ${BMX_QT}/QtApp.h ${BMX_QT}/QtMain.cpp ${BMX_QT}/QtKeymap.cpp ${BMX_QT}/QtDialogs.cpp
        ${BMX_QT}/QtTest.cpp ${BMX_QT}/QtTheme.cpp
        ${BMX_QT}/QtProperties.cpp ${BMX_QT}/QtShortcuts.cpp ${BMX_QT}/QtScreenGL.cpp ${BMX_QT}/QtDebugger.cpp)
    set_target_properties(qbluemsx PROPERTIES AUTOMOC ON)
    # The core has a field named "slots": keep Qt keywords as Q_SLOTS, Q_EMIT...
    target_compile_definitions(qbluemsx PRIVATE QT_NO_KEYWORDS)
    target_link_libraries(qbluemsx PRIVATE bluemsx-theme Qt6::Widgets Qt6::OpenGLWidgets)
endif()

# ---- Data -------------------------------------------------------------------

# The build tree mirrors the install layout (bin/ + share/bluemsx/), so the
# executables find their data without installing.
set(BMX_DATA_DIRS Machines Databases "Keyboard Config" "Shortcut Profiles" Themes)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/share/bluemsx)
foreach(_d IN LISTS BMX_DATA_DIRS)
    file(CREATE_LINK "${BMX}/ReleaseFiles/${_d}" "${CMAKE_BINARY_DIR}/share/bluemsx/${_d}" SYMBOLIC)
    install(DIRECTORY "${BMX}/ReleaseFiles/${_d}" DESTINATION ${CMAKE_INSTALL_DATADIR}/bluemsx)
endforeach()
# Artwork of the built-in Classic themes (Windows resources on Win32).
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/share/bluemsx/Classic)
foreach(_d classic classic_dark)
    file(CREATE_LINK "${BMX}/Src/Resources/${_d}" "${CMAKE_BINARY_DIR}/share/bluemsx/Classic/${_d}" SYMBOLIC)
    install(DIRECTORY "${BMX}/Src/Resources/${_d}" DESTINATION ${CMAKE_INSTALL_DATADIR}/bluemsx/Classic)
endforeach()
