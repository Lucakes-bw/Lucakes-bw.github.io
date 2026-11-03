add_library(wordfreq
    tokenizer.cpp
    counter.cpp
)
add_library(wordfreq::wordfreq ALIAS wordfreq)

# PUBLIC：wordfreq 自己编译时要用，链接 wordfreq 的目标也要用
target_include_directories(wordfreq PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_features(wordfreq PUBLIC cxx_std_17)

find_package(Threads REQUIRED)
target_link_libraries(wordfreq PRIVATE Threads::Threads)   # PRIVATE：只有 counter.cpp 用到了线程

wordfreq_set_warnings(wordfreq)
