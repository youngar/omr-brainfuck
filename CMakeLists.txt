cmake_minimum_required(VERSION 3.2 FATAL_ERROR)

project(bf VERSION 1.0 LANGUAGES CXX C)

set(OMR_COMPILER   ON  CACHE INTERNAL "Enable the Compiler.")
set(OMR_JIT ON CACHE INTERNAL "")
set(OMR_JITBUILDER ON CACHE INTERNAL "")
set(OMR_THREAD ON CACHE INTERNAL "")

add_subdirectory(omr)

add_executable(bf bf.cpp)

target_link_libraries(bf PRIVATE imperium)
