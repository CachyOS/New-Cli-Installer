cmake_minimum_required(VERSION 3.21)
project(CachyOS-Installer)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_COMPILER "/usr/bin/clang")
set(CMAKE_CXX_COMPILER "/usr/bin/clang++")

set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined")

set(CMAKE_C_FLAGS "-fno-rtti -Wno-non-virtual-dtor -Wno-strict-aliasing -Wl,-Bdynamic -Wno-unused-command-line-argument")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++20")

set(CMAKE_C_FLAGS_RELEASE "-flto=thin -gsplit-dwarf -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-math-errno -fno-ident -Wl,--strip-all -Wl,--discard-all")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")

set(CMAKE_C_FLAGS_DEBUG "-DDEBUG -g")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")

file(GLOB_RECURSE SOURCES "${CMAKE_SOURCE_DIR}/include/*.cpp" "${CMAKE_SOURCE_DIR}/resources/*.cpp" "${CMAKE_SOURCE_DIR}/src/*.cpp")

find_package(nlohmann_json REQUIRED)
find_package(ftxui REQUIRED)

add_executable(CachyOS-Installer ${SOURCES} src/utils.h src/utils.cpp src/definitions.h)

target_include_directories(CachyOS-Installer PRIVATE "${CMAKE_SOURCE_DIR}/include/")
target_link_libraries(CachyOS-Installer PRIVATE ftxui::screen PRIVATE ftxui::dom PRIVATE ftxui::component nlohmann_json::nlohmann_json)
target_link_options(CachyOS-Installer PRIVATE LINKER:--no-undefined)
