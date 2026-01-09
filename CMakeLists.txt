cmake_minimum_required(VERSION 3.20.0)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/model)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(ei-zephyr-camera-inference
    DESCRIPTION "Edge Impulse Camera Inferencing Zephyr Example"
    VERSION 1.0.0
    LANGUAGES C CXX ASM
    )

include(utils/cmake/utils.cmake)

# Needed for colorful output
zephyr_compile_options(-fdiagnostics-color=always)
zephyr_compile_definitions(
    EI_CLASSIFIER_ALLOCATION_STATIC
    EI_TENSOR_ARENA_LOCATION=".ext_ram.bss")

# Project include directories
set(INCLUDES
    .
    src
    )
include_directories(${INCLUDES})

# add all sources to the project
target_sources(app PRIVATE src/main.cpp)
target_sources(app PRIVATE src/inference/inferencing.cpp)
target_sources(app PRIVATE src/sensors/ei_camera.cpp)
target_sources(app PRIVATE src/sensors/camera_drv.c)
target_sources(app PRIVATE src/memory_mgmt.cpp)