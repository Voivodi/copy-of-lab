cmake_minimum_required(VERSION 3.20)

project(HamArc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)

add_subdirectory(bin)

enable_testing()
add_subdirectory(tests)
