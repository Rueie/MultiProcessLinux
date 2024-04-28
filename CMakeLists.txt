cmake_minimum_required(VERSION 2.6)

set(PROJECT gauss)

PROJECT(${PROJECT})
set(GAUSS_DIR Gauss)
set(GAUSS_LIB gauss_lib)
set(SOURCE_DIR src)
set(HEADERS_DIR inc)

file(GLOB_RECURSE SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE_DIR}/*.cpp)
file(GLOB_RECURSE HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/${HEADERS_DIR}/*.h)

add_executable(${PROJECT} ${HEADERS} ${SOURCES})

target_include_directories(${PROJECT} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/${HEADERS_DIR})

add_subdirectory(${GAUSS_DIR})
target_link_libraries(${PROJECT} ${GAUSS_LIB})
target_include_directories(${PROJECT} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/${GAUSS_DIR})

#Добавление lrt библиотеки для раделённой памяти
find_library(LIBRT rt)
target_link_libraries(${PROJECT} ${LIBRT})
