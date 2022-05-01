cmake_minimum_required(VERSION 2.8)
project(pamix)

include(CMakeGitDefines.cmake)

option(WITH_UNICODE "Compile with unicode support and symbols" ON)
set(NCURSES_H_INCLUDE_FILE "ncurses.h" CACHE STRING "ncursesw include directive content")

set(CMAKE_CXX_STANDARD 11)
file(GLOB_RECURSE pamix_SRC
        "include/*.h"
        "src/*.cpp")

include_directories("include")
link_libraries("pulse" "pthread")

find_package(PkgConfig REQUIRED QUIET)
IF (WITH_UNICODE)
    pkg_search_module(NCURSESW REQUIRED ncursesw)
    link_libraries(${NCURSESW_LDFLAGS})
    add_definitions(-DFEAT_UNICODE)
    set(NCURSES_H_INCLUDE ${NCURSESW_INCLUDEDIR}/${NCURSES_H_INCLUDE_FILE} CACHE STRING "ncursesw include directive content")
	 message(STATUS "Set NCURSES_H_INCLUDE to ${NCURSES_H_INCLUDE}")
ELSE ()
    pkg_search_module(NCURSES REQUIRED ncurses)
    set(NCURSES_H_INCLUDE ${NCURSES_INCLUDEDIR}/${NCURSES_H_INCLUDE_FILE} CACHE STRING "ncursesw include directive content")
    link_libraries(${NCURSES_LDFLAGS})
	 message(STATUS "Set NCURSES_H_INCLUDE to ${NCURSES_H_INCLUDE}")
ENDIF ()

configure_file(config.hpp.in ${CMAKE_CURRENT_SOURCE_DIR}/config.hpp )
configure_file(man/pamix.1.in ${CMAKE_CURRENT_SOURCE_DIR}/man/pamix.1)

add_executable(pamix ${pamix_SRC})
install(FILES pamix.conf DESTINATION /etc/xdg)
install(TARGETS pamix DESTINATION bin)
install(FILES man/pamix.1 DESTINATION share/man/man1)

