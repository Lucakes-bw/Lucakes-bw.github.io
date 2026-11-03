# wordfreq_set_warnings(<目标>)：给自己写的目标打开一组警告。
# 只用在自己的目标上：第三方库的警告不是我们能改的。
function(wordfreq_set_warnings target)
    if(MSVC)
        set(warnings /W4 /permissive-)
        set(werror /WX)
    else()
        set(warnings -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
                     -Wold-style-cast -Wnon-virtual-dtor -Woverloaded-virtual)
        set(werror -Werror)
    endif()
    target_compile_options(${target} PRIVATE ${warnings})
    if(WORDFREQ_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE ${werror})
    endif()
endfunction()
