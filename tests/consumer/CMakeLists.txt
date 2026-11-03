cmake_minimum_required(VERSION 3.25)
project(cachyos-installer-consumer LANGUAGES CXX)

find_package(cachyos-installer 0.12 REQUIRED COMPONENTS gucc installer)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE cachyos::installer)
