include(FetchContent)

# 下载的第三方库都加上 SYSTEM：它们的头文件用 -isystem 引入，里面的警告不会算到我们头上

# fmt：先找系统中已经安装的（Homebrew、apt、vcpkg……），版本太旧或者没有，再下载源代码
find_package(fmt 10 QUIET)
if(fmt_FOUND)
    message(STATUS "使用已安装的 fmt ${fmt_VERSION}")
else()
    message(STATUS "没有找到 fmt 10 以上的版本，下载源代码")
    FetchContent_Declare(fmt
        URL https://github.com/fmtlib/fmt/archive/refs/tags/11.2.0.tar.gz
        URL_HASH SHA256=bc23066d87ab3168f27cef3e97d545fa63314f5c79df5ea444d41d56f962c6af
        SYSTEM)
    FetchContent_MakeAvailable(fmt)
endif()

# CLI11：只有头文件，直接下载
FetchContent_Declare(CLI11
    URL https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.5.0.tar.gz
    URL_HASH SHA256=17e02b4cddc2fa348e5dbdbb582c59a3486fa2b2433e70a0c3bacb871334fd55
    SYSTEM)
FetchContent_MakeAvailable(CLI11)
