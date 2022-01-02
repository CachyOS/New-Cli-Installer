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
include(Linker)
include(StaticAnalyzers)
include(Sanitizers)
include(FetchContent)

find_package(PkgConfig REQUIRED)
pkg_check_modules(
  LIBNM
  REQUIRED
  IMPORTED_TARGET
  libnm>=1.10.6)

pkg_check_modules(
  GLIBMM
  REQUIRED
  IMPORTED_TARGET
  glibmm-2.4>=2.56.0)

FetchContent_Declare(ftxui
  GIT_REPOSITORY "https://github.com/vnepogodin/ftxui.git"
  GIT_TAG "constexpr"
)
FetchContent_Declare(fmt
  GIT_REPOSITORY "https://github.com/fmtlib/fmt.git"
  GIT_TAG "3a951a66cb0fb53ff5a9d5ce9c77e05ef9d382ce"
)
FetchContent_MakeAvailable(fmt)
FetchContent_Declare(spdlog
  GIT_REPOSITORY "https://github.com/gabime/spdlog.git"
  GIT_TAG "cc30229abb1e22ebe1f8657de4011a53db7bd6ac"
)
FetchContent_Declare(simdjson
  GIT_REPOSITORY "https://github.com/simdjson/simdjson.git"
  GIT_TAG "3a93e45dd166d8e614a33b0ba4da39c1a01660b7"
)
FetchContent_Declare(cpr
  GIT_REPOSITORY "https://github.com/libcpr/cpr.git"
  GIT_TAG "beb9e98806bb84bcc130a2cebfbcbbc6ce62b335"
)
FetchContent_MakeAvailable(ftxui spdlog simdjson cpr)
if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
   FetchContent_Declare(range-v3
     GIT_REPOSITORY "https://github.com/ericniebler/range-v3.git"
     GIT_TAG "9aa032ccd0eb4bd77f58e5b181168f1038c222c6"
   )
   FetchContent_MakeAvailable(range-v3)
endif()

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
   src/view.hpp
   src/definitions.hpp
   src/screen_service.hpp src/screen_service.cpp
   src/config.cpp src/config.hpp
   src/utils.cpp src/utils.hpp
   src/widgets.cpp src/widgets.hpp
   src/follow_process_log.hpp src/follow_process_log.cpp
   src/tui.cpp src/tui.hpp
   src/main.cpp
   )

add_executable(test-exec-interactive
   src/config.cpp src/config.hpp
   src/utils.cpp src/utils.hpp
   src/widgets.cpp src/widgets.hpp
   src/follow_process_log.hpp src/follow_process_log.cpp
   src/tui.cpp src/tui.hpp
   src/main_test.cpp
   )

add_executable(test-process-tailing
   src/config.cpp src/config.hpp
   src/utils.cpp src/utils.hpp
   src/widgets.cpp src/widgets.hpp
   src/follow_process_log.hpp src/follow_process_log.cpp
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

target_link_libraries(${PROJECT_NAME} PRIVATE project_warnings project_options spdlog::spdlog fmt::fmt ftxui::screen ftxui::dom ftxui::component simdjson::simdjson cpr::cpr PkgConfig::GLIBMM)
target_link_libraries(test-exec-interactive PRIVATE project_warnings project_options spdlog::spdlog fmt::fmt ftxui::component simdjson::simdjson cpr::cpr PkgConfig::GLIBMM)
target_link_libraries(test-process-tailing PRIVATE project_warnings project_options spdlog::spdlog fmt::fmt ftxui::component simdjson::simdjson cpr::cpr PkgConfig::GLIBMM)

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
