cmake_minimum_required(VERSION 3.20)
project(kartoza-screencaster VERSION 0.9.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
    Multimedia
    MultimediaWidgets
    Concurrent
)

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
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::Concurrent
)

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
    )

    macro(add_unit_test name source)
        add_executable(${name} ${source} ${TEST_SOURCES})
        target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Test Qt6::Multimedia Qt6::Widgets)
        target_include_directories(${name} PRIVATE src)
        add_test(NAME ${name} COMMAND ${name})
    endmacro()

    add_unit_test(test_merger tests/test_merger.cpp)
    add_unit_test(test_config tests/test_config.cpp)
    add_unit_test(test_monitor tests/test_monitor.cpp)
endif()
