cmake_minimum_required(VERSION 3.16)
project(parity_harness C CXX)

# 独立工程：只编译对照 harness，不牵扯项目主 CMake（主 CMake 还没做 API 改名）。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")

set(PROJ_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(JOY_ROOT "${PROJ_ROOT}/third_party/joy")
set(JOY_INC "${JOY_ROOT}/include")
set(SPINE_INC "${JOY_ROOT}/spine/spine-c/include")

add_executable(parity_harness parity.cpp
    "${JOY_ROOT}/src/jcollision.c"
    "${JOY_ROOT}/src/jlut.c")
target_include_directories(parity_harness PRIVATE
    "${PROJ_ROOT}"              # entt.hpp
    "${JOY_INC}"                # <joy/jmath.h>
    "${JOY_INC}/joy"            # "joy.h"
    "${SPINE_INC}"
    "${JOY_ROOT}"               # external/klib/*
    "${JOY_ROOT}/src")          # jcollision.h
target_compile_features(parity_harness PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(parity_harness PRIVATE /utf-8 /FS)
endif()
