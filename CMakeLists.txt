cmake_minimum_required(VERSION 3.15)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

##
## PROJECT
## name and version
##
project(cachyos-installer
        VERSION 0.5.0
        LANGUAGES CXX)

##
## INCLUDE
##
include(GNUInstallDirs)
include(StandardProjectSettings)
include(CompilerWarnings)
include(EnableCcache)
include(Linker)
include(StaticAnalyzers)
include(Sanitizers)
include(CPM)

#find_package(PkgConfig REQUIRED)
#pkg_check_modules(
#  GLIBMM
#  REQUIRED
#  IMPORTED_TARGET
#  glibmm-2.4>=2.56.0)

CPMAddPackage(
  NAME ftxui
  GITHUB_REPOSITORY vnepogodin/ftxui
  GIT_TAG "c++20"
  EXCLUDE_FROM_ALL YES
)
CPMAddPackage(
  NAME fmt
  GITHUB_REPOSITORY fmtlib/fmt
  GIT_TAG 70dc3de053b6814c023faebbd21525aab575d25d
  EXCLUDE_FROM_ALL YES
)
CPMAddPackage(
  NAME spdlog
  GITHUB_REPOSITORY gabime/spdlog
  GIT_TAG 2f2d04b3e840428a18942ca2d3d65203ec564647
  EXCLUDE_FROM_ALL YES
)
CPMAddPackage(
  NAME simdjson
  GITHUB_REPOSITORY simdjson/simdjson
  GIT_TAG 6698eb96b99576b8d53f8d90c9023717379e1c0f
  EXCLUDE_FROM_ALL YES
)
CPMAddPackage(
  NAME cpr
  GITHUB_REPOSITORY libcpr/cpr
  GIT_TAG da381bb2afe4e36b18b1fe206bc06951d8fdc06e
  EXCLUDE_FROM_ALL YES
)
if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
   CPMAddPackage(
     NAME range-v3
     GITHUB_REPOSITORY ericniebler/range-v3
     GIT_TAG 9aa032ccd0eb4bd77f58e5b181168f1038c222c6
     EXCLUDE_FROM_ALL YES
   )
endif()

##
## CONFIGURATION
##
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto")

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
   set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fwhole-program -fuse-linker-plugin")
endif()

# Link this 'library' to set the c++ standard / compile-time options requested
add_library(project_options INTERFACE)
target_compile_features(project_options INTERFACE cxx_std_20)

##
## Target
##
add_executable(${PROJECT_NAME}
   src/view.hpp
   src/definitions.hpp
   src/screen_service.hpp src/screen_service.cpp
   src/config.cpp src/config.hpp
   src/utils.cpp src/utils.hpp
   src/disk.cpp src/disk.hpp
   src/drivers.cpp src/drivers.hpp
   src/widgets.cpp src/widgets.hpp
   src/follow_process_log.hpp src/follow_process_log.cpp
   src/crypto.cpp src/crypto.hpp
   src/misc.cpp src/misc.hpp
   src/tui.cpp src/tui.hpp
   src/main.cpp
   )

add_executable(test-exec-interactive
   src/config.cpp src/config.hpp
   src/utils.cpp src/utils.hpp
   src/disk.cpp src/disk.hpp
   src/widgets.cpp src/widgets.hpp
   src/follow_process_log.hpp src/follow_process_log.cpp
   src/crypto.cpp src/crypto.hpp
   src/tui.cpp src/tui.hpp
   src/main_test.cpp
   )

add_executable(test-process-tailing
   src/config.cpp src/config.hpp
   src/utils.cpp src/utils.hpp
   src/disk.cpp src/disk.hpp
   src/widgets.cpp src/widgets.hpp
   src/follow_process_log.hpp src/follow_process_log.cpp
   src/crypto.cpp src/crypto.hpp
   src/tui.cpp src/tui.hpp
   src/test_proccess_tailing.cpp
   )

# Link this 'library' to use the warnings specified in CompilerWarnings.cmake
add_library(project_warnings INTERFACE)
set_project_warnings(project_warnings)

# Add linker configuration
configure_linker(project_options)

# sanitizer options if supported by compiler
enable_sanitizers(project_options)

include_directories(${CMAKE_SOURCE_DIR}/src)

target_link_libraries(${PROJECT_NAME} PRIVATE project_warnings project_options spdlog::spdlog fmt::fmt ftxui::screen ftxui::dom ftxui::component simdjson::simdjson cpr::cpr)
target_link_libraries(test-exec-interactive PRIVATE project_warnings project_options spdlog::spdlog fmt::fmt ftxui::component simdjson::simdjson cpr::cpr)
target_link_libraries(test-process-tailing PRIVATE project_warnings project_options spdlog::spdlog fmt::fmt ftxui::component simdjson::simdjson cpr::cpr)

if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
   target_link_libraries(${PROJECT_NAME} PRIVATE range-v3::range-v3)
   target_link_libraries(test-exec-interactive PRIVATE range-v3::range-v3)
endif()

option(ENABLE_UNITY "Enable Unity builds of projects" OFF)
if(ENABLE_UNITY)
   # Add for any project you want to apply unity builds for
   set_target_properties(${PROJECT_NAME} PROPERTIES UNITY_BUILD ON)
endif()

install(
   TARGETS ${PROJECT_NAME}
   RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
