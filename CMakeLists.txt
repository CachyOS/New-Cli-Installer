cmake_minimum_required(VERSION 3.15)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

##
## PROJECT
## name and version
##
project(cachyos-installer
        VERSION 0.9.4
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
  GIT_TAG c1e0b1197b7c1825b61f8cddd5491df507769ada
  EXCLUDE_FROM_ALL YES
)
CPMAddPackage(
  NAME fmt
  GITHUB_REPOSITORY fmtlib/fmt
  GIT_TAG 12.1.0
  EXCLUDE_FROM_ALL YES
)
CPMAddPackage(
  NAME tomlplusplus
  GITHUB_REPOSITORY marzer/tomlplusplus
  GIT_TAG 7807a314ad1c0c9c227d9dbd68f23f187163372e
  EXCLUDE_FROM_ALL YES
)
CPMAddPackage(
  NAME spdlog
  GITHUB_REPOSITORY gabime/spdlog
  GIT_TAG v1.17.0
  EXCLUDE_FROM_ALL YES
)
CPMAddPackage(
  NAME rapidjson
  GITHUB_REPOSITORY Tencent/rapidjson
  GIT_TAG 24b5e7a8b27f42fa16b96fc70aade9106cf7102f
  EXCLUDE_FROM_ALL YES
)
CPMAddPackage(
  NAME cpr
  GITHUB_REPOSITORY libcpr/cpr
  GIT_TAG 1.14.1
  EXCLUDE_FROM_ALL YES
)
CPMAddPackage(
  NAME ctre
  GITHUB_REPOSITORY hanickadot/compile-time-regular-expressions
  GIT_TAG v3.10.0
  EXCLUDE_FROM_ALL YES
)
CPMAddPackage(
   NAME doctest
   GITHUB_REPOSITORY doctest/doctest
   GIT_TAG v2.4.12
   EXCLUDE_FROM_ALL YES
)

# FIXME(vnepogodin): temp hack for ftxui
target_compile_options(screen PRIVATE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-deprecated;-Wno-float-equal>
    $<$<CXX_COMPILER_ID:GNU>:-Wno-deprecated-declarations>
)

##
## CONFIGURATION
##
if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
   set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto=thin -fwhole-program-vtables")
   set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -flto=thin -fwhole-program-vtables")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
   set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto -fwhole-program -fuse-linker-plugin")
endif()

# Link this 'library' to set the c++ standard / compile-time options requested
add_library(project_options INTERFACE)
target_compile_features(project_options INTERFACE cxx_std_23)

##
## Target
##
add_executable(${PROJECT_NAME}
   src/view.hpp
   src/definitions.hpp
   src/config.cpp src/config.hpp
   src/utils.cpp src/utils.hpp
   src/disk.cpp src/disk.hpp
   src/drivers.cpp src/drivers.hpp
   src/widgets.cpp src/widgets.hpp
   src/follow_process_log.hpp src/follow_process_log.cpp
   src/crypto.cpp src/crypto.hpp
   src/misc.cpp src/misc.hpp
   src/simple_tui.cpp src/simple_tui.hpp
   src/tui.cpp src/tui.hpp
   src/main.cpp
   )

# Link this 'library' to use the warnings specified in CompilerWarnings.cmake
add_library(project_warnings INTERFACE)
set_project_warnings(project_warnings)

# Add linker configuration
configure_linker(project_options)

# sanitizer options if supported by compiler
enable_sanitizers(project_options)

# Prepare RapidJSON (RapidJSON is a header-only library)
set(RAPIDJSON_INCLUDE_DIR "${rapidjson_SOURCE_DIR}/include")

include_directories(${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/gucc/include ${RAPIDJSON_INCLUDE_DIR})

add_subdirectory(gucc)

if(COS_INSTALLER_BUILD_TESTS)
   add_subdirectory(tests)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE project_warnings project_options spdlog::spdlog fmt::fmt ftxui::screen ftxui::dom ftxui::component ctre::ctre tomlplusplus::tomlplusplus gucc::gucc)
if(NOT ENABLE_DEVENV)
  target_link_libraries(${PROJECT_NAME} PRIVATE cpr::cpr)
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
