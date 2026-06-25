cmake_minimum_required(VERSION 3.20)
project(kartoza-screencaster VERSION 2.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Debug for local development
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    message(STATUS "Build type: Debug (default - use -DCMAKE_BUILD_TYPE=Release for optimized builds)")
endif()

# Use ccache if available
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
endif()

# Compiler warnings (GCC/Clang only — MSVC uses /W4 by default)
if(NOT MSVC)
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

# Release: optimize and strip
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O2)
    add_link_options(-s)  # strip symbols
endif()

# Export compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Pass version to source code
add_compile_definitions(APP_VERSION="${PROJECT_VERSION}")
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
    Multimedia
    MultimediaWidgets
    Concurrent
    Svg
    Network
)

# D-Bus is used for two Linux-only features: the xdg-desktop-portal
# screencast/screenshot capture path, and the session-bus remote
# control service. QtDBus actually ships on Windows/macOS too, but
# desktop D-Bus IPC is a Linux-only ecosystem in practice, and the
# Linux-only system headers (unistd.h, fcntl.h, etc.) we use in the
# portal implementation make this an unambiguous gate.
find_package(Qt6 COMPONENTS DBus QUIET)
if(Qt6DBus_FOUND AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_compile_definitions(HAS_DBUS)
    set(HAS_DBUS_FEATURE TRUE)
endif()

set(SOURCES
    src/main.cpp
    src/gui/mainwindow.cpp
    src/gui/mainwindow.h
    src/gui/recordpage.cpp
    src/gui/recordpage.h
    src/gui/canvas.cpp
    src/gui/canvas.h
    src/gui/historypage.cpp
    src/gui/historypage.h
    src/gui/settingspage.cpp
    src/gui/settingspage.h
    src/gui/processingpage.cpp
    src/gui/processingpage.h
    src/gui/assetgallery.cpp
    src/gui/assetgallery.h
    src/gui/tray.cpp
    src/gui/tray.h
    src/recorder/recorder.cpp
    src/recorder/recorder.h
    src/merger/merger.cpp
    src/merger/merger.h
    src/monitor/monitor.cpp
    src/monitor/monitor.h
    src/webcam/webcam.cpp
    src/webcam/webcam.h
    src/config/config.cpp
    src/config/config.h
    src/platform/platform.cpp
    src/platform/platform.h
    src/youtube/youtube.cpp
    src/youtube/youtube.h
)

if(HAS_DBUS_FEATURE)
    list(APPEND SOURCES src/dbus/dbusservice.cpp src/dbus/dbusservice.h)
    list(APPEND SOURCES src/portal/portal.cpp src/portal/portal.h)
endif()

qt6_add_resources(RESOURCES resources/resources.qrc)
add_executable(${PROJECT_NAME} ${SOURCES} ${RESOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::Concurrent
    Qt6::Svg
    Qt6::Network
)

if(HAS_DBUS_FEATURE)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::DBus)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE src)

install(TARGETS ${PROJECT_NAME} DESTINATION bin)
install(FILES resources/kartoza-screencaster.desktop DESTINATION share/applications)
install(FILES resources/icon_ready.svg DESTINATION share/icons/hicolor/scalable/apps
        RENAME kartoza-screencaster.svg)

# Tests
find_package(Qt6 COMPONENTS Test QUIET)
if(Qt6Test_FOUND)
    enable_testing()

    # Shared sources used by tests (non-GUI modules only)
    set(TEST_SOURCES
        src/merger/merger.cpp
        src/merger/merger.h
        src/recorder/recorder.cpp
        src/recorder/recorder.h
        src/config/config.cpp
        src/config/config.h
        src/monitor/monitor.cpp
        src/monitor/monitor.h
        src/webcam/webcam.cpp
        src/webcam/webcam.h
        src/platform/platform.cpp
        src/platform/platform.h
        src/youtube/youtube.cpp
        src/youtube/youtube.h
    )

    # Portal pulls in QtDBus and is referenced from recorder.cpp under
    # HAS_DBUS, so tests that link recorder.cpp must also link Portal.
    if(HAS_DBUS_FEATURE)
        list(APPEND TEST_SOURCES src/portal/portal.cpp src/portal/portal.h)
    endif()

    macro(add_unit_test name source)
        add_executable(${name} ${source} ${TEST_SOURCES})
        target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Test Qt6::Multimedia Qt6::Widgets Qt6::Network)
        if(HAS_DBUS_FEATURE)
            target_link_libraries(${name} PRIVATE Qt6::DBus)
        endif()
        target_include_directories(${name} PRIVATE src)
        add_test(NAME ${name} COMMAND ${name})
    endmacro()

    add_unit_test(test_merger tests/test_merger.cpp)
    add_unit_test(test_config tests/test_config.cpp)
    add_unit_test(test_monitor tests/test_monitor.cpp)
    add_unit_test(test_pipeline tests/test_pipeline.cpp)
    add_unit_test(test_recorder tests/test_recorder.cpp)
    add_unit_test(test_youtube tests/test_youtube.cpp)
    add_unit_test(test_merger_exhaustive tests/test_merger_exhaustive.cpp)
    target_compile_definitions(test_merger_exhaustive PRIVATE SRCDIR="${CMAKE_SOURCE_DIR}")

    # History test needs historypage source
    add_executable(test_history tests/test_history.cpp
        src/gui/historypage.cpp src/gui/historypage.h
        ${TEST_SOURCES})
    target_link_libraries(test_history PRIVATE Qt6::Core Qt6::Test Qt6::Multimedia Qt6::MultimediaWidgets Qt6::Widgets Qt6::Concurrent)
    if(HAS_DBUS_FEATURE)
        target_link_libraries(test_history PRIVATE Qt6::DBus)
    endif()
    target_include_directories(test_history PRIVATE src)
    add_test(NAME test_history COMMAND test_history)

    # Canvas test needs GUI sources
    add_executable(test_canvas tests/test_canvas.cpp
        src/gui/canvas.cpp src/gui/canvas.h
        ${TEST_SOURCES})
    target_link_libraries(test_canvas PRIVATE Qt6::Core Qt6::Test Qt6::Multimedia Qt6::Widgets Qt6::Concurrent)
    if(HAS_DBUS_FEATURE)
        target_link_libraries(test_canvas PRIVATE Qt6::DBus)
    endif()
    target_include_directories(test_canvas PRIVATE src)
    add_test(NAME test_canvas COMMAND test_canvas)
endif()

# --- CPack / Debian package ---
set(CPACK_PACKAGE_NAME "kartoza-screencaster")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Screen recording tool with WYSIWYG canvas and YouTube upload")
set(CPACK_PACKAGE_CONTACT "info@kartoza.com")
set(CPACK_PACKAGE_VENDOR "Kartoza")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/kartoza/kartoza-screencaster")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Kartoza <info@kartoza.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "video")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libqt6core6 (>= 6.2), libqt6widgets6 (>= 6.2), libqt6multimedia6 (>= 6.2), libqt6network6 (>= 6.2), libqt6svg6 (>= 6.2), ffmpeg")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "pulseaudio")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "grim, wl-screenrec")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
    "Kartoza Screencaster - Professional screen recording tool.\n Features include WYSIWYG canvas layout, webcam overlay, logo placement,\n pause/resume, post-processing with audio normalization, and YouTube upload.\n Made with love by Kartoza (https://kartoza.com).")

include(CPack)

# Doxygen documentation
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating Doxygen API documentation"
        VERBATIM
    )
    add_custom_target(docs-open
        COMMAND xdg-open ${CMAKE_SOURCE_DIR}/build/doxygen/html/index.html
        DEPENDS docs
        COMMENT "Opening Doxygen docs in browser"
    )
endif()
