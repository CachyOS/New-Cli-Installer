# Set a default build type if none was specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'RelWithDebInfo' as none was specified.")
  set(CMAKE_BUILD_TYPE
      RelWithDebInfo
      CACHE STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui, ccmake
  set_property(
    CACHE CMAKE_BUILD_TYPE
    PROPERTY STRINGS
             "Debug"
             "Release"
             "MinSizeRel"
             "RelWithDebInfo")
endif()

set(CMAKE_CXX_EXTENSIONS OFF)

set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE INTERNAL "" FORCE)
set(FTXUI_BUILD_EXAMPLES OFF CACHE INTERNAL "" FORCE)
set(FTXUI_ENABLE_INSTALL OFF CACHE INTERNAL "" FORCE)
set(SPDLOG_FMT_EXTERNAL ON CACHE INTERNAL "" FORCE)
set(SPDLOG_DISABLE_DEFAULT_LOGGER ON CACHE INTERNAL "" FORCE)
set(SIMDJSON_DISABLE_DEPRECATED_API ON CACHE INTERNAL "" FORCE)

# Generate compile_commands.json to make it easier to work with clang based tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_IPO "Enable Interprocedural Optimization, aka Link Time Optimization (LTO)" OFF)

if(ENABLE_IPO)
  include(CheckIPOSupported)
  check_ipo_supported(
    RESULT
    result
    OUTPUT
    output)
  if(result)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
  else()
    message(SEND_ERROR "IPO is not supported: ${output}")
  endif()
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
  add_compile_options(-fcolor-diagnostics)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_compile_options(-fdiagnostics-color=always)
else()
  message(STATUS "No colored compiler diagnostic set for '${CMAKE_CXX_COMPILER_ID}' compiler.")
endif()

# Enables STL container checker if not building a release.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_definitions(-D_GLIBCXX_ASSERTIONS)
endif()

# Enables dev environment.
option(ENABLE_DEVENV "Enable dev environment" ON)
if(NOT ENABLE_DEVENV)
  add_definitions(-DNDEVENV)
endif()
