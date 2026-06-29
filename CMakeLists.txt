cmake_minimum_required(VERSION 3.16)

project(raytracer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(raytracer main.cpp)
target_link_libraries(raytracer PRIVATE Threads::Threads)

# Copy image assets next to the executable so texture loading works from build/.
file(COPY ${CMAKE_SOURCE_DIR}/images DESTINATION ${CMAKE_BINARY_DIR})
