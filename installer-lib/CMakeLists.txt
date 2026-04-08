cmake_minimum_required(VERSION 3.15)

##
## PROJECT
## name and version
##
project(cachyos-installer-lib
        VERSION 0.1.0
        LANGUAGES CXX)

add_library(${PROJECT_NAME}
   src/system.cpp include/cachyos/system.hpp
   src/config.cpp include/cachyos/config.hpp
   src/disk.cpp include/cachyos/disk.hpp
   src/packages.cpp include/cachyos/packages.hpp
   src/bootloader.cpp include/cachyos/bootloader.hpp
   src/crypto.cpp include/cachyos/crypto.hpp
   src/validation.cpp include/cachyos/validation.hpp
   src/installer_data.cpp include/cachyos/installer_data.hpp
   include/cachyos/types.hpp
   include/cachyos/installer.hpp
   )

add_library(cachyos::installer-lib ALIAS ${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(${PROJECT_NAME} PUBLIC gucc::gucc)
target_link_libraries(${PROJECT_NAME} PRIVATE project_warnings project_options spdlog::spdlog fmt::fmt cpr::cpr)
