add_executable(wordfreq_cli main.cpp)
set_target_properties(wordfreq_cli PROPERTIES OUTPUT_NAME wordfreq)   # 生成的程序叫 wordfreq
target_link_libraries(wordfreq_cli PRIVATE wordfreq::wordfreq fmt::fmt CLI11::CLI11)
wordfreq_set_warnings(wordfreq_cli)
