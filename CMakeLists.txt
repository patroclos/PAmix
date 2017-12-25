cmake_minimum_required(VERSION 2.8)
project(pamix)

set(CMAKE_CXX_STANDARD 11)
file(GLOB_RECURSE pamix_SRC
  "include/*.h"
  "src/*.cpp")

include_directories("include")
link_libraries("pulse" "pthread" "RxCpp")

IF(WITH_UNICODE)
  link_libraries("ncursesw")
  add_definitions(-DFEAT_UNICODE)
ELSE()
  link_libraries("ncurses")
ENDIF()

add_executable(pamix ${pamix_SRC})
install(FILES pamix.conf DESTINATION /etc)
install(TARGETS pamix DESTINATION bin)
install(FILES man/pamix.1 DESTINATION share/man/man1)