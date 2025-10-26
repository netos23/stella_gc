cmake_minimum_required(VERSION 3.29)
project(stella_project LANGUAGES C)

set(CMAKE_C_STANDARD 99)


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(TEST_SOURCES_LIST test/custom_test #[[test/stella-tests/ok]])
set(GENERATED_TEST_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/test/generated_sources)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test/generated_sources)

include_directories(stella-runtime/include)
add_subdirectory(stella-runtime)

add_compile_definitions(STELLA_DEBUG)
add_compile_definitions(STELLA_GC_STATS)
add_compile_definitions(STELLA_RUNTIME_STATS)

foreach (STELLA_TEST_GROUP ${TEST_SOURCES_LIST})
    message(STATUS "Generating test group ${STELLA_TEST_GROUP}")
    file(GLOB TEST_SOURCES LIST_DIRECTORIES true "${STELLA_TEST_GROUP}/*")
    foreach (STELLA_TEST_PATH ${TEST_SOURCES})
        get_filename_component(
                STELLA_TEST_NAME
                ${STELLA_TEST_PATH}
                NAME_WE
        )
        get_filename_component(
                STELLA_TEST_FULL_NAME
                ${STELLA_TEST_PATH}
                NAME
        )
        message(STATUS "Processing test file in group ${STELLA_TEST_NAME}")
        message(STATUS "Processing test file in group path  ${STELLA_TEST_PATH}")
        add_custom_command(
                COMMAND docker run -i fizruk/stella compile < ${STELLA_TEST_FULL_NAME} > ${GENERATED_TEST_SOURCES}/${STELLA_TEST_NAME}.c
                DEPENDS ${STELLA_TEST_PATH}
                OUTPUT ${GENERATED_TEST_SOURCES}/${STELLA_TEST_NAME}.c
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${STELLA_TEST_GROUP}
        )
        add_executable(${STELLA_TEST_NAME} ${GENERATED_TEST_SOURCES}/${STELLA_TEST_NAME}.c)
        target_link_libraries(${STELLA_TEST_NAME} stella_runtime)
    endforeach (STELLA_TEST_PATH)
endforeach (STELLA_TEST_GROUP)



