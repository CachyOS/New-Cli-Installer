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

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build shared libraries by default
set(BUILD_SHARED_LIBS ON CACHE INTERNAL "" FORCE)

set(FTXUI_BUILD_DOCS OFF CACHE INTERNAL "" FORCE)
set(FTXUI_BUILD_EXAMPLES OFF CACHE INTERNAL "" FORCE)
set(FTXUI_ENABLE_INSTALL OFF CACHE INTERNAL "" FORCE)
set(SPDLOG_FMT_EXTERNAL ON CACHE INTERNAL "" FORCE)
set(SPDLOG_DISABLE_DEFAULT_LOGGER ON CACHE INTERNAL "" FORCE)
set(SIMDJSON_DISABLE_DEPRECATED_API ON CACHE INTERNAL "" FORCE)
set(CPR_USE_SYSTEM_CURL ON CACHE INTERNAL "" FORCE)
# works only with libc++ so far
#set(CTRE_MODULE ON CACHE INTERNAL "" FORCE)
# for some reason CTRE doesnt respect CMAKE_CXX_STANDARD
set(CTRE_CXX_STANDARD 23 CACHE INTERNAL "" FORCE)

# fix rapidjson
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE INTERNAL "" FORCE)

# Generate compile_commands.json to make it easier to work with clang based tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build with PIC
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(CMAKE_C_COMPILER_ID MATCHES ".*Clang")
   set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -flto=thin -fwhole-program-vtables")
   set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -flto=thin -fwhole-program-vtables")
elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
   set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -flto -fwhole-program -fuse-linker-plugin")
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
   set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto=thin -fwhole-program-vtables")
   set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -flto=thin -fwhole-program-vtables")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
   set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto -fwhole-program -fuse-linker-plugin")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
  #add_compile_options(-nostdlib++ -stdlib=libc++ -nodefaultlibs -fexperimental-library)
  #add_link_options(-stdlib=libc++)

  add_compile_options(-fstrict-vtable-pointers)

  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
    # Set new experimental pass manager, it's a performance, build time and binary size win.
    # Can be removed after https://reviews.llvm.org/D66490 merged and released to at least two versions of clang.
    add_compile_options(-fexperimental-new-pass-manager)
  endif()
endif()

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

# Builds as statically linked
option(COS_BUILD_STATIC "Build all static" OFF)
if(COS_BUILD_STATIC)
  add_definitions(-DCOS_BUILD_STATIC)
  set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "" FORCE)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")# -static")
endif()

# Enables STL container checker if not building a release.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_definitions(-D_GLIBCXX_ASSERTIONS)
  add_definitions(-D_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS=1)
  add_definitions(-D_LIBCPP_ENABLE_ASSERTIONS=1)
endif()

# Enables dev environment.
option(ENABLE_DEVENV "Enable dev environment" ON)
if(NOT ENABLE_DEVENV)
  add_definitions(-DNDEVENV)
endif()

# Enables tests.
option(COS_INSTALLER_BUILD_TESTS "Enable dev environment" OFF)

# Get the Git revision
include(GitVersion)
add_definitions(-DINSTALLER_VERSION="${GIT_VERSION}")
