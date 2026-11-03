# WORDFREQ_SANITIZE=address,undefined 或 thread：所有目标（包括第三方库）都加上 -fsanitize
if(WORDFREQ_SANITIZE)
    if(MSVC)
        add_compile_options(/fsanitize=${WORDFREQ_SANITIZE})
    else()
        add_compile_options(-fsanitize=${WORDFREQ_SANITIZE} -fno-omit-frame-pointer -fno-sanitize-recover=all)
        add_link_options(-fsanitize=${WORDFREQ_SANITIZE})
    endif()
endif()

# WORDFREQ_COVERAGE：Clang 的源码级覆盖率，运行后生成 .profraw 文件
if(WORDFREQ_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "WORDFREQ_COVERAGE 需要 Clang，当前编译器是 ${CMAKE_CXX_COMPILER_ID}")
    endif()
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate)
endif()
