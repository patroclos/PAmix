cmake_minimum_required(VERSION 2.8)
project(pamix C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
file(GLOB_RECURSE pamix_SRC
        "src/*.h"
        "src/*.c")

include_directories("src")
link_libraries("pulse" "pthread")

find_package(PkgConfig REQUIRED QUIET)
pkg_search_module(NCURSESW REQUIRED ncursesw)
link_libraries(${NCURSESW_LIBRARIES})
add_definitions(${NCURSESW_CFLAGS} ${NCURSESW_CFLAGS_OTHER})

add_executable(pamix ${pamix_SRC})
install(FILES pamix.conf DESTINATION /etc/xdg)
install(TARGETS pamix DESTINATION bin)
install(FILES man/pamix.1 TYPE MAN)

