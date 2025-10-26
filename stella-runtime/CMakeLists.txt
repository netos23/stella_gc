cmake_minimum_required(VERSION 3.29)
project(
        stella_runtime
        VERSION 1.0.0
        DESCRIPTION "Basic runtime implementation for stella language"
        LANGUAGES C
)

add_library(stella_runtime)

set(CMAKE_C_STANDARD 99)

set(public_headers include/gc.h include/runtime.h)
set(sources src/gc.c src/runtime.c)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${sources})

target_sources(stella_runtime PRIVATE ${sources})
target_include_directories(stella_runtime PUBLIC include/)


set_target_properties(stella_runtime PROPERTIES
        PUBLIC_HEADER "${public_headers}"
        SOVERSION ${PROJECT_VERSION_MAJOR}
        VERSION ${PROJECT_VERSION})


