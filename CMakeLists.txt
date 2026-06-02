cmake_minimum_required(VERSION 3.16)
project(MixedEditing)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS "${CMAKE_SOURCE_DIR}/widget/ui")


find_package(Qt6 COMPONENTS
        Core
        Gui
        Widgets
        Svg
        REQUIRED)

add_executable(MixedEditing main.cpp
        widget/source/mainwindow.cpp
        widget/include/mainwindow.h
        widget/ui/mainwindow.ui
        widget/resources/icons.qrc)

if(APPLE)
    enable_language(OBJCXX)
    target_sources(MixedEditing PRIVATE widget/source/mac_titlebar.mm)
    target_link_libraries(MixedEditing "-framework AppKit")
endif()

target_link_libraries(MixedEditing
        Qt::Core
        Qt::Gui
        Qt::Widgets
        Qt::Svg
)

