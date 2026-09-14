cmake_minimum_required(VERSION 3.20.0)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/model/CMakeLists.txt)
    message(FATAL_ERROR
        "No model in model/. Export an image impulse from Edge Impulse Studio "
        "(Deployment > Zephyr library > Build) and unzip it into model/. "
        "See model/README.md.")
endif()

list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/model)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(ei-zephyr-camera-inference)

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
