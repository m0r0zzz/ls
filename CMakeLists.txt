cmake_minimum_required (VERSION 2.6)

project (ls)

add_executable(ls ls.c)

target_compile_options(ls PRIVATE "-std=gnu99")

install(TARGETS ls DESTINATION /usr/bin/)
