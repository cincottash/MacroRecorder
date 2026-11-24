cmake_minimum_required(VERSION 3.21)

# The app's name lives here (and nowhere else): rename it in one place.
set(APP_NAME "MacroRecorder")
set(APP_ORG "cincottash")
project(${APP_NAME} VERSION 0.1.0 LANGUAGES CXX)
# Release builds pass the real version (e.g. -DRELEASE_VERSION=1.2.3); local builds show 0.1.0-dev.
set(RELEASE_VERSION "" CACHE STRING "Version shown in the app (set by the release workflow)")
if(RELEASE_VERSION)
    set(APP_VERSION "${RELEASE_VERSION}")
else()
    set(APP_VERSION "${PROJECT_VERSION}-dev")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# STATIC_BUILD=ON (with a static Qt) makes one self-contained exe: Qt and every library it uses are linked in,
# so nothing has to be installed next to it. Development builds leave it OFF for faster linking.
option(STATIC_BUILD "Link everything statically (release exe)" OFF)
if(STATIC_BUILD AND MINGW)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a)          # prefer libfoo.a over the DLL import library libfoo.dll.a
    set(PKG_CONFIG_USE_STATIC_LIBS ON)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
    # Libraries the static Qt dependencies (OpenSSL's libcrypto, FreeType) need but don't declare. They must come
    # last on the link line, after the libraries that use them.
    set(CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_CXX_STANDARD_LIBRARIES} -lbz2 -lbrotlidec -lbrotlicommon -lws2_32 -lcrypt32 -liphlpapi -lbcrypt")
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Svg Network)
if(STATIC_BUILD AND MINGW AND TARGET harfbuzz::harfbuzz)
    # harfbuzz's CMake package always points at its DLL; use the static library instead.
    find_library(HARFBUZZ_STATIC NAMES libharfbuzz.a REQUIRED)
    set_target_properties(harfbuzz::harfbuzz PROPERTIES IMPORTED_LOCATION "${HARFBUZZ_STATIC}" IMPORTED_IMPLIB "${HARFBUZZ_STATIC}")
    # ...and everything the static harfbuzz needs (graphite2, glib, ...), as its pkg-config file lists them.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HB_STATIC REQUIRED harfbuzz)
    foreach(lib IN LISTS HB_STATIC_STATIC_LIBRARIES)
        string(APPEND CMAKE_CXX_STANDARD_LIBRARIES " -l${lib}")
    endforeach()
endif()
qt_standard_project_setup()

set(SOURCES
    src/core/Action.cpp
    src/core/Expr.cpp
    src/core/ImageFind.cpp
    src/core/Paths.cpp
    src/core/Player.cpp
    src/core/Recorder.cpp
    src/platform/Backend.cpp
    src/app/ThemeData.cpp
    src/app/Themes.cpp
    src/app/main.cpp
)
file(GLOB APP_SOURCES CONFIGURE_DEPENDS src/app/*.cpp)
list(APPEND SOURCES ${APP_SOURCES})
list(REMOVE_DUPLICATES SOURCES)

if(WIN32)
    list(APPEND SOURCES src/platform/win/WinBackend.cpp src/platform/win/WinKeys.cpp src/platform/win/WinOcr.cpp packaging/app.rc)
    # The build doesn't see that app.rc uses icon.ico: without this, a new icon keeps the old one in the exe
    set_source_files_properties(packaging/app.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon.ico)
else()
    list(APPEND SOURCES src/platform/linux/LinuxBackend.cpp src/platform/linux/X11Backend.cpp
                        src/platform/linux/UinputInjector.cpp src/platform/linux/LinuxKeys.cpp)
endif()

qt_add_executable(app WIN32 ${SOURCES})
set_target_properties(app PROPERTIES OUTPUT_NAME ${APP_NAME})
target_include_directories(app PRIVATE src)
target_compile_definitions(app PRIVATE
    APP_NAME="${APP_NAME}" APP_ORG="${APP_ORG}" APP_VERSION="${APP_VERSION}")
target_link_libraries(app PRIVATE Qt6::Widgets Qt6::Svg Qt6::Network)

if(STATIC_BUILD)
    # Only the image plugins the app uses (SVG for icons and style-sheet images); the others (TIFF, WebP, JPEG...)
    # would pull in many more libraries for nothing.
    qt_import_plugins(app
        INCLUDE_BY_TYPE imageformats Qt6::QSvgPlugin
        INCLUDE_BY_TYPE iconengines Qt6::QSvgIconPlugin)
    if(WIN32)
        # The normal Windows display plugin, plus "offscreen" for invisible tests (QT_QPA_PLATFORM=offscreen).
        qt_import_plugins(app INCLUDE_BY_TYPE platforms Qt6::QWindowsIntegrationPlugin Qt6::QOffscreenIntegrationPlugin)
        # HTTPS (Help > Check for Updates) through Windows' own TLS: no OpenSSL to ship. No network information plugin.
        qt_import_plugins(app INCLUDE_BY_TYPE tls Qt6::QSchannelBackendPlugin EXCLUDE_BY_TYPE networkinformation)
    endif()
endif()

qt_add_resources(app resources PREFIX "/" BASE resources FILES
    resources/check.svg resources/check_dark.svg resources/icon.svg
    resources/down_light.svg resources/down_dark.svg resources/up_light.svg resources/up_dark.svg)

if(WIN32)
    target_link_libraries(app PRIVATE user32 dwmapi winmm ole32 uuid runtimeobject)
    target_compile_definitions(app PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MINGW)
        # No compiler runtime DLLs next to the exe; strip debug symbols from release builds.
        target_link_options(app PRIVATE -static-libgcc -static-libstdc++ $<$<CONFIG:Release>:-s>)
    endif()
endif()

if(UNIX AND NOT APPLE)
    # X11 (XRecord + XTest, both in libXtst) for X11 sessions and XWayland; /dev/uinput needs only kernel headers.
    find_package(X11 REQUIRED)
    if(NOT X11_Xtst_FOUND)
        message(FATAL_ERROR "libXtst (XTest/XRecord) development files are missing: install libxtst-dev")
    endif()
    target_link_libraries(app PRIVATE X11::X11 X11::Xtst)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(app PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

install(TARGETS app RUNTIME DESTINATION bin)
