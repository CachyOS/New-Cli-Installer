cmake_minimum_required(VERSION 3.15)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

##
## PROJECT
## name and version
##
project(cachyos-installer
        VERSION 0.0.1
        LANGUAGES CXX)

##
## INCLUDE
##
include(GNUInstallDirs)
include(StandardProjectSettings)
include(CompilerWarnings)
include(EnableCcache)
include(ClangTidy)
include(FetchContent)

FetchContent_Declare(ftxui
  GIT_REPOSITORY "https://github.com/arthursonzogni/ftxui.git"
  GIT_TAG "cecd54df42dd66fdf8386ed461e16b725bffc827"
)
FetchContent_MakeAvailable(ftxui)

FetchContent_Declare(fmt
  GIT_REPOSITORY "https://github.com/fmtlib/fmt.git"
  GIT_TAG "491ba2dda5a04c2438abb6bd90219de773793ec0"
)
FetchContent_MakeAvailable(fmt)

FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY "https://github.com/ArthurSonzogni/nlohmann_json_cmake_fetchcontent.git"
  GIT_TAG "v3.10.4"
)
FetchContent_MakeAvailable(nlohmann_json)

##
## CONFIGURATION
##
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto")

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fwhole-program")
endif()

# Link this 'library' to set the c++ standard / compile-time options requested
add_library(project_options INTERFACE)
target_compile_features(project_options INTERFACE cxx_std_20)

##
## Target
##
add_executable(${PROJECT_NAME}
    src/definitions.hpp
    src/utils.cpp src/utils.hpp
    src/main.cpp
    )

# Link this 'library' to use the warnings specified in CompilerWarnings.cmake
add_library(project_warnings INTERFACE)
set_project_warnings(project_warnings)

include_directories(${CMAKE_SOURCE_DIR}/src)

target_link_libraries(${PROJECT_NAME} PRIVATE project_warnings project_options fmt::fmt ftxui::screen ftxui::dom ftxui::component nlohmann_json::nlohmann_json)

option(ENABLE_UNITY "Enable Unity builds of projects" OFF)
if(ENABLE_UNITY)
  # Add for any project you want to apply unity builds for
  set_target_properties(${PROJECT_NAME} PROPERTIES UNITY_BUILD ON)
endif()

install(
    TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)


# uninstall
add_custom_target(uninstall
    COMMAND cat ${PROJECT_BINARY_DIR}/install_manifest.txt | xargs rm
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
