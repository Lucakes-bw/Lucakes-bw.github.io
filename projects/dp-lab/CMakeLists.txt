# 设计模式专栏的配套代码：每篇文章一个目录，目录下每个 .cpp 编译成一个程序
# 以 _ce.cpp 结尾的文件是故意编译不过的演示（compile error），不参与构建
# 第一行注释里写着 C++20 的文件用 C++20 编译，其余用 C++17
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
find_package(Threads REQUIRED)        # 单例等章节的程序用到了 std::thread

file(GLOB chapters RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/[0-9][0-9]-*)
foreach(dir ${chapters})
  file(GLOB sources ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.cpp)
  list(FILTER sources EXCLUDE REGEX "_ce\\.cpp$")
  foreach(src ${sources})
    get_filename_component(name ${src} NAME_WE)
    add_executable(${dir}_${name} ${src})
    target_link_libraries(${dir}_${name} PRIVATE Threads::Threads)
    file(STRINGS ${src} first LIMIT_COUNT 1)
    if(first MATCHES "C\\+\\+20")             # 第一行注释写着 C++20 的程序用 C++20 编译
      set_target_properties(${dir}_${name} PROPERTIES CXX_STANDARD 20)
    endif()
    set_target_properties(${dir}_${name} PROPERTIES OUTPUT_NAME ${name} RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${dir})
  endforeach()
endforeach()
