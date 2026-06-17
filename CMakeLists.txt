cmake_minimum_required(VERSION 3.20)

project(trt LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "LLVM ${LLVM_PACKAGE_VERSION}")

add_executable(trt
    src/main.cpp
    src/lexer.cpp
)

target_include_directories(trt PRIVATE
    ${LLVM_INCLUDE_DIRS}
)

separate_arguments(
    LLVM_DEFINITIONS_LIST
    NATIVE_COMMAND
    ${LLVM_DEFINITIONS}
)

target_compile_definitions(
    trt PRIVATE
    ${LLVM_DEFINITIONS_LIST}
)

target_link_libraries(trt PRIVATE LLVM)

# Custom compile flags per configuration
target_compile_options(trt PRIVATE
    $<$<CONFIG:Debug>:-O0 -g>
    $<$<CONFIG:Release>:-O3 -g0>
)
