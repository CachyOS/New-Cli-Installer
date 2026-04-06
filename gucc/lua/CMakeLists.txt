cmake_minimum_required(VERSION 3.15)

project(gucc-lua VERSION 0.0.1 LANGUAGES CXX)

find_package(Lua 5.4 REQUIRED EXACT)

if(NOT TARGET sol2::sol2)
    find_package(sol2 CONFIG QUIET)
    if(NOT sol2_FOUND)
        add_library(sol2::sol2 INTERFACE IMPORTED)
    endif()
endif()

add_library(${PROJECT_NAME} SHARED
    src/bindings.cpp
    src/pacmanconf.cpp
    src/initcpio.cpp
    src/fstab.cpp
    src/bootloader.cpp
    src/user.cpp
    src/locale.cpp
    src/partitioning.cpp
    src/kernel_params.cpp
    src/services.cpp
    src/mount.cpp
    src/file_utils.cpp
    src/package_profiles.cpp
    src/cpu.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(${PROJECT_NAME} SYSTEM BEFORE PUBLIC ${LUA_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC gucc::gucc ${LUA_LIBRARIES} sol2::sol2)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "gucc"
    PREFIX ""
    SUFFIX ".so"
)

install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/lua/5.4)
