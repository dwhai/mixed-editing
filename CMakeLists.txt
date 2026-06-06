cmake_minimum_required(VERSION 3.16)
project(MixedEditing)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)


find_package(Qt6 COMPONENTS
        Core
        Gui
        Widgets
        Svg
        Network
        Multimedia
        OpenGL
        OpenGLWidgets
        Sql
        REQUIRED)

# 查找 FFmpeg 7
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
        libavcodec
        libavformat
        libavutil
        libavfilter
        libswscale
        libswresample
)

# 查找 libwebp：该 Qt 安装未自带 webp 图片插件，而开眼 CDN 的封面统一返回 webp，
# 需用 libwebp 解码作为 QPixmap 的回退路径。
pkg_check_modules(WEBP REQUIRED libwebp)

# 自动扫描源文件和头文件
file(GLOB_RECURSE SOURCES
        "main.cpp"
        "src/widget/resource/*.cpp"
        "src/widget/source/*.cpp"
        "src/ffmpeg/source/*.cpp"
        "src/network/source/*.cpp"
        "src/db/source/*.cpp"
)

file(GLOB_RECURSE HEADERS
        "src/widget/include/*.h"
        "src/ffmpeg/include/*.h"
        "src/network/include/*.h"
        "src/db/include/*.h"
)

# 资源文件
set(QRC_FILES
        src/resources/icons.qrc
)

add_executable(MixedEditing
        ${SOURCES}
        ${HEADERS}
        ${QRC_FILES}
)

# 包含 FFmpeg 头文件路径
target_include_directories(MixedEditing PRIVATE
        ${FFMPEG_INCLUDE_DIRS}
        ${WEBP_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/src/ffmpeg/include
        ${CMAKE_SOURCE_DIR}/src/db/include
)

if(APPLE)
    enable_language(OBJCXX)
    target_sources(MixedEditing PRIVATE src/widget/source/mac_titlebar.mm)
    target_link_libraries(MixedEditing "-framework AppKit")
endif()

# FFmpeg 链接选项（避免重复链接）
target_link_libraries(MixedEditing
        Qt::Core
        Qt::Gui
        Qt::Widgets
        Qt::Svg
        Qt::Network
        Qt::Multimedia
        Qt::OpenGL
        Qt::OpenGLWidgets
        Qt::Sql
        ${FFMPEG_LINK_LIBRARIES}
        ${WEBP_LINK_LIBRARIES}
)

