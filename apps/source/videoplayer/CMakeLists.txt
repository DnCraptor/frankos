set(PICO_BOARD pico2 CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)

set(PICO_NO_GC_SECTIONS 1 CACHE BOOL "Disable -ffunction-sections to reduce ELF section count")

project(video C ASM)
pico_sdk_init()

set(CMAKE_C_STANDARD 11)

set(COMPILED_DIR "${CMAKE_SOURCE_DIR}/../../../sdcard/fos")

add_executable(${PROJECT_NAME}
    main.c
    frankos_stubs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../api/m-os-api-sdtfn.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../api/m-os-api-math.c
)

target_include_directories(${PROJECT_NAME} BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../api
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../api/FreeRTOS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../api
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../api/mos2/system
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_options(${PROJECT_NAME} PRIVATE
    -Xlinker
    --print-memory-usage
    -r
    -Wl,--force-group-allocation
)

if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
target_compile_options(${PROJECT_NAME} PUBLIC
    -Os
    -ffixed-r9
    -fno-builtin-printf
    -mno-unaligned-access
    -Werror=implicit-function-declaration
    -include _ansi.h
    -Wno-sign-compare
    -Wno-unused-parameter
)
endif()

# Read version from version.txt ("major minor" format)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../../version.txt" _VERSION_LINE)
list(GET _VERSION_LINE 0 _VERSION_LINE)
string(REPLACE " " ";" _VERSION_PARTS "${_VERSION_LINE}")
list(GET _VERSION_PARTS 0 FRANK_VER_MAJOR)
list(GET _VERSION_PARTS 1 FRANK_VER_MINOR)
if(FRANK_VER_MINOR LESS 10)
    set(FRANK_VERSION_STR "${FRANK_VER_MAJOR}.0${FRANK_VER_MINOR}")
else()
    set(FRANK_VERSION_STR "${FRANK_VER_MAJOR}.${FRANK_VER_MINOR}")
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    configSTACK_DEPTH_TYPE=uint32_t
    FRANK_VERSION_STR="${FRANK_VERSION_STR}"
    NDEBUG
)

pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_enable_stdio_usb(${PROJECT_NAME} 0)

# Post-build: merge sections, strip, and copy to sdcard/fos
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_LINKER}
        -r --force-group-allocation
        -T ${CMAKE_CURRENT_SOURCE_DIR}/merge.ld
        -o ${CMAKE_CURRENT_BINARY_DIR}/video_merged.elf
        $<TARGET_FILE:${PROJECT_NAME}>
    COMMAND ${CMAKE_STRIP} --strip-debug
        ${CMAKE_CURRENT_BINARY_DIR}/video_merged.elf
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_BINARY_DIR}/video_merged.elf
        ${COMPILED_DIR}/video
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_SOURCE_DIR}/video.inf"
        ${COMPILED_DIR}/video.inf
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_SOURCE_DIR}/videoplayer.ico"
        ${COMPILED_DIR}/video.ico
)
