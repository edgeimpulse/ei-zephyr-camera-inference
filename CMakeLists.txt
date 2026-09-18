cmake_minimum_required(VERSION 3.20.0)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The Edge Impulse model is a Zephyr module exported from Studio. By default we
# pick up the FOMO face detection export sitting next to this application in the
# west workspace; a local model/ directory wins if present, and -DEI_MODEL_DIR=<path>
# overrides both.
if(NOT DEFINED EI_MODEL_DIR)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/model/zephyr/module.yml)
        set(EI_MODEL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/model)
    else()
        set(EI_MODEL_DIR
            ${CMAKE_CURRENT_SOURCE_DIR}/../face-detection-fomo-zephyr-module-v217-fomo-96x96)
    endif()
endif()
get_filename_component(EI_MODEL_DIR "${EI_MODEL_DIR}" ABSOLUTE)

if(NOT EXISTS ${EI_MODEL_DIR}/zephyr/module.yml)
    message(FATAL_ERROR
        "No Edge Impulse model module found at ${EI_MODEL_DIR}.\n"
        "Export your impulse as a 'Zephyr module' from Edge Impulse Studio and either\n"
        "drop it in ${CMAKE_CURRENT_SOURCE_DIR}/model or pass -DEI_MODEL_DIR=<path>.")
endif()

message(STATUS "Edge Impulse model module: ${EI_MODEL_DIR}")
list(APPEND ZEPHYR_EXTRA_MODULES ${EI_MODEL_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(ei-zephyr-camera-inference
    DESCRIPTION "Edge Impulse camera inferencing on Zephyr"
    VERSION 1.0.0
    LANGUAGES C CXX ASM
    )

include(utils/cmake/utils.cmake)

# Needed for colorful output
zephyr_compile_options(-fdiagnostics-color=always)

# Project include directories
set(INCLUDES
    .
    src
    )
include_directories(${INCLUDES})

# add all sources to the project
target_sources(app PRIVATE src/main.cpp)
target_sources(app PRIVATE src/camera/ei_camera.cpp)
target_sources(app PRIVATE src/inference/inferencing.cpp)
target_sources(app PRIVATE src/porting/ei_heap.cpp)
