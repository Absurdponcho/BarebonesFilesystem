cmake_minimum_required (VERSION 3.20)
cmake_policy(VERSION 3.20)

project (
    BarebonesFilesystem
    VERSION 0.1.0
)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

include(CheckCSourceCompiles)
include(CheckSymbolExists)
include(cmake/fetch_dokan.cmake)

file(READ ${CMAKE_CURRENT_LIST_DIR}/cmake/check_builtin.c CHECK_BUILTIN_SRC)
set(CMAKE_REQUIRED_DEFINITIONS "-DBUILTIN_FUNC=__builtin_trap")
check_c_source_compiles("${CHECK_BUILTIN_SRC}" HAS_BUILTIN_TRAP)
unset(CMAKE_REQUIRED_DEFINITIONS)
check_symbol_exists(strerror_s "string.h" HAS_STRERROR_S)
check_symbol_exists(strerror_r "string.h" HAS_STRERROR_R)

set(GENERATED_HEADERS_DIR "${CMAKE_BINARY_DIR}/generated_headers")
configure_file(${CMAKE_CURRENT_LIST_DIR}/cmake/config.h.in ${GENERATED_HEADERS_DIR}/config.h)

set (FSLIB_INCLUDE_PATH ${CMAKE_CURRENT_LIST_DIR}/FsLib/include)

aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/FsLib/source FSLIB_SRC)
aux_source_directory(${FSLIB_INCLUDE_PATH} FSLIB_INCLUDE)
add_library(
    FsLib
    STATIC
    ${FSLIB_SRC}
    ${FSLIB_INCLUDE}
)

target_compile_options(
    FsLib  PRIVATE
    -DCMAKE
    -DFS_LIB
)

target_include_directories(
    FsLib PRIVATE 
    ${GENERATED_HEADERS_DIR}
)

if(NOT WIN32)
    return()
endif()

fetch_dokan()

aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/WindowsImpl/ MAIN_SRC)
add_executable(
    FilesystemTest
    ${MAIN_SRC}
)

target_include_directories(
    FilesystemTest PRIVATE 
    ${GENERATED_HEADERS_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/WindowsImpl/ThirdParty/Dokany/include
    ${FSLIB_INCLUDE_PATH}
)

target_compile_options(
    FilesystemTest PRIVATE
    -DCMAKE
)

target_link_libraries(
    FilesystemTest
    PRIVATE
    FsLib
    ${DOKAN_LIB}
)