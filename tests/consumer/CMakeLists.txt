cmake_minimum_required(VERSION 3.25)
project(cachyos-installer-consumer LANGUAGES CXX)

# COS_EXPECTED_VERSION is passed by the consumer-build test as the project's
# MAJOR.MINOR, as a downstream would request it, so the check follows version
# bumps. Unset, any installed version is accepted.
find_package(cachyos-installer ${COS_EXPECTED_VERSION} REQUIRED COMPONENTS gucc installer)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE cachyos::installer)
