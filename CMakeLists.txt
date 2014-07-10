project(bqueue)
cmake_minimum_required(VERSION 2.8)
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR
    "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR
    "${CMAKE_CXX_COMPILER_ID}" STREQUAL "ICC")
    set(warnings "-Wall -Wextra -Werror")
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    set(warnings "/W4 /WX /EHsc")
endif()
option(WITH_BQUEUE_ERRNO "Enables thread-local error handling" ON)
if(WITH_BQUEUE_ERRNO)
    add_definitions("-DBQUEUE_WITH_ERRNO")
endif()
include_directories("./inc")
aux_source_directory("./src" SRC_LIST)
add_library(${PROJECT_NAME} ${SRC_LIST})

