# 设计模式专栏的配套代码：每篇文章一个目录，目录下每个 .cpp 编译成一个程序
#   cmake -B build && cmake --build build
#   ./build/02-uml/lifetime
cmake_minimum_required(VERSION 3.16)
project(dp_lab CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()
add_compile_options(-Wall -Wextra)

file(GLOB chapters RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/[0-9][0-9]-*)
foreach(dir ${chapters})
  file(GLOB sources ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.cpp)
  foreach(src ${sources})
    get_filename_component(name ${src} NAME_WE)
    add_executable(${dir}_${name} ${src})
    set_target_properties(${dir}_${name} PROPERTIES OUTPUT_NAME ${name} RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${dir})
  endforeach()
endforeach()
