cmake_minimum_required(VERSION 3.16)
project(MixedEditing)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS "${CMAKE_SOURCE_DIR}/src/widget/ui")


find_package(Qt6 COMPONENTS
        Core
        Gui
        Widgets
        Svg
        REQUIRED)

# 查找 FFmpeg 7
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
        libavcodec
        libavformat
        libavutil
        libswscale
        libswresample
)

add_executable(MixedEditing main.cpp
        src/resource/mainwindow.cpp
        src/widget/include/mainwindow.h
        src/widget/ui/mainwindow.ui
        src/widget/resources/icons.qrc
        src/ffmpeg/include/FFmpegEncoder.h
        src/ffmpeg/source/FFmpegEncoder.cpp)

# 包含 FFmpeg 头文件路径
target_include_directories(MixedEditing PRIVATE
        ${FFMPEG_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/src/ffmpeg/include
)

if(APPLE)
    enable_language(OBJCXX)
    target_sources(MixedEditing PRIVATE src/resource/mac_titlebar.mm)
    target_link_libraries(MixedEditing "-framework AppKit")
endif()

# FFmpeg 链接选项（避免重复链接）
target_link_libraries(MixedEditing
        Qt::Core
        Qt::Gui
        Qt::Widgets
        Qt::Svg
        ${FFMPEG_LINK_LIBRARIES}
)

