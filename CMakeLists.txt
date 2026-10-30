cmake_minimum_required(VERSION 3.16)
project(obs-lyrics-dock VERSION 12.9.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 COMPONENTS Core Widgets Network QUIET)
if(Qt6_FOUND)
    set(QT_VERSION 6)
    message(STATUS "Using Qt6")
else()
    find_package(Qt5 COMPONENTS Core Widgets Network REQUIRED)
    set(QT_VERSION 5)
    message(STATUS "Using Qt5")
endif()

set(PLUGIN_SOURCES
    src/plugin-main.cpp
    src/lyrics-dock.cpp          src/lyrics-dock.hpp
    src/song-manager.cpp         src/song-manager.hpp
    src/song-edit-dialog.cpp     src/song-edit-dialog.hpp
    src/lyrics-renderer.cpp      src/lyrics-renderer.hpp
    src/media-gallery.cpp        src/media-gallery.hpp
    src/mv-tab.cpp               src/mv-tab.hpp
    src/ppt-tab.cpp              src/ppt-tab.hpp
    src/overlay-tab.cpp          src/overlay-tab.hpp
    src/preview-window.cpp       src/preview-window.hpp
    src/layout-dialog.cpp        src/layout-dialog.hpp
    src/color-preset-button.cpp  src/color-preset-button.hpp
    src/websocket-server.cpp     src/websocket-server.hpp
    src/settings-dialog.cpp      src/settings-dialog.hpp
    src/ws-tab.cpp               src/ws-tab.hpp
    src/pinyin-engine.cpp        src/pinyin-engine.hpp
                                 src/pinyin-table.hpp
                                 src/qrcodegen.hpp
    src/audio-bin.cpp             src/audio-bin.hpp
)

add_library(obs-lyrics-dock MODULE ${PLUGIN_SOURCES})
set_target_properties(obs-lyrics-dock PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)

if(QT_VERSION EQUAL 6)
    target_link_libraries(obs-lyrics-dock PRIVATE
        OBS::libobs OBS::obs-frontend-api
        Qt6::Core Qt6::Widgets Qt6::Network
        ole32 oleaut32)
else()
    target_link_libraries(obs-lyrics-dock PRIVATE
        libobs obs-frontend-api
        Qt5::Core Qt5::Widgets Qt5::Network
        ole32 oleaut32)
endif()

target_include_directories(obs-lyrics-dock PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(WIN32)
    install(TARGETS obs-lyrics-dock
        RUNTIME DESTINATION "obs-plugins/64bit"
        LIBRARY DESTINATION "obs-plugins/64bit")
    install(DIRECTORY data/
        DESTINATION "data/obs-plugins/obs-lyrics-dock")
endif()

message(STATUS "obs-lyrics-dock v12.9.0 configured")
