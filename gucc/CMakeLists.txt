cmake_minimum_required(VERSION 3.6)

##
## PROJECT
## name and version
##
project(gucc
        VERSION 0.0.1
        LANGUAGES CXX)

add_library(${PROJECT_NAME} SHARED
   #src/utils.cpp src/utils.hpp
   src/string_utils.cpp src/string_utils.hpp
   src/file_utils.cpp src/file_utils.hpp
   src/cpu.cpp src/cpu.hpp
   src/pacmanconf_repo.cpp src/pacmanconf_repo.hpp
   src/initcpio.cpp src/initcpio.hpp
   #src/chwd_profiles.cpp src/chwd_profiles.hpp
   #src/disk.cpp src/disk.hpp
   )

add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_DIR}/src)
target_link_libraries(${PROJECT_NAME} PUBLIC project_warnings project_options spdlog::spdlog fmt::fmt range-v3::range-v3)

if(COS_INSTALLER_BUILD_TESTS)
   add_subdirectory(tests)
endif()
