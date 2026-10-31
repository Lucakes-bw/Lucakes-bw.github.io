cmake_minimum_required(VERSION 3.16)
project(QtChat VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Sql)
qt_standard_project_setup()

set(SOURCES
    main.cpp
    connection.h connection.cpp
    chatclient.h chatclient.cpp
    messagestore.h messagestore.cpp
    contactmodel.h contactmodel.cpp
    messagemodel.h messagemodel.cpp
    bubbledelegate.h bubbledelegate.cpp
    logindialog.h logindialog.cpp
    mainwindow.h mainwindow.cpp
)

# 开发用：用脚本驱动界面、自动截图（见 devtools.cpp），发布时关掉
option(CHAT_DEVTOOLS "编译脚本驱动的测试工具" OFF)
if(CHAT_DEVTOOLS)
    list(APPEND SOURCES devtools.h devtools.cpp)
endif()

qt_add_executable(QtChat MACOSX_BUNDLE WIN32 ${SOURCES})
target_link_libraries(QtChat PRIVATE Qt6::Widgets Qt6::Network Qt6::Sql)
if(CHAT_DEVTOOLS)
    target_compile_definitions(QtChat PRIVATE CHAT_DEVTOOLS)
endif()

set_target_properties(QtChat PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.lucakes-bw.qtchat
    MACOSX_BUNDLE_BUNDLE_NAME QtChat
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)
