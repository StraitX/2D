cmake_minimum_required(VERSION 3.10)

project(StraitX2D)

set(CMAKE_CXX_STANDARD 14)

set(SX_2D_SOURCES_DIR ${PROJECT_SOURCE_DIR}/sources)
set(SX_2D_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(SX_2D_THIRDPARTY_DIR ${PROJECT_SOURCE_DIR}/thirdparty)

set(SX_2D_SOURCES
    ${SX_2D_SOURCES_DIR}/rect_renderer.cpp
    ${SX_2D_SOURCES_DIR}/common/semaphore_ring.cpp
)

add_library(StraitX2D STATIC ${SX_2D_SOURCES})
target_link_libraries(StraitX2D PUBLIC StraitXBase)
target_include_directories(StraitX2D PUBLIC ${SX_2D_INCLUDE_DIR})