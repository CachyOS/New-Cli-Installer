include_guard(GLOBAL)

include(GenerateExportHeader)

set(_COS_LIBRARY_DIR ${CMAKE_CURRENT_LIST_DIR})

# cos_add_library(<name> EXPORT_MACRO <PREFIX> EXPORT_HEADER <relpath> NAMESPACE <ns> SOURCES <src>...)
#
# Compiles <src> once into <name>_obj with hidden visibility, then links it as:
#   <name>         the installed library (shared unless BUILD_SHARED_LIBS is off)
#   <name>_static  for unit tests and fuzzers: they reach non-exported internals,
#                  which still resolve in a static link. Aliases <name> when
#                  <name> is already static.
# EXPORT_HEADER is generated under the build include dir
# and defines <EXPORT_MACRO>_EXPORT_BEGIN/_END for public headers.
function(cos_add_library name)
  cmake_parse_arguments(ARG "" "EXPORT_HEADER;EXPORT_MACRO;NAMESPACE" "SOURCES" ${ARGN})

  add_library(${name}_obj OBJECT ${ARG_SOURCES})
  set_target_properties(${name}_obj PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON)

  set(_cos_export_regions "
#define ${ARG_EXPORT_MACRO}_EXPORT_BEGIN _Pragma(\"GCC visibility push(default)\")
#define ${ARG_EXPORT_MACRO}_EXPORT_END _Pragma(\"GCC visibility pop\")
")
  generate_export_header(${name}_obj
    BASE_NAME ${ARG_EXPORT_MACRO}
    EXPORT_MACRO_NAME ${ARG_EXPORT_MACRO}_EXPORT
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/include/${ARG_EXPORT_HEADER}
    CUSTOM_CONTENT_FROM_VARIABLE _cos_export_regions)

  add_library(${name} $<TARGET_OBJECTS:${name}_obj>)
  get_target_property(type ${name} TYPE)
  set_target_properties(${name} PROPERTIES VERSION ${PROJECT_VERSION})
  if(type STREQUAL "SHARED_LIBRARY")
    # every minor release may break ABI
    set_target_properties(${name} PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR})
    # static third-party archives stay internal
    set(COS_EXPORT_NAMESPACE ${ARG_NAMESPACE})
    string(MAKE_C_IDENTIFIER "${ARG_EXPORT_MACRO}_${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}" COS_EXPORT_VERSION_NODE)
    set(map ${CMAKE_CURRENT_BINARY_DIR}/${name}.map)
    configure_file(${_COS_LIBRARY_DIR}/exports.map.in ${map} @ONLY)
    target_link_options(${name} PRIVATE LINKER:--exclude-libs,ALL LINKER:--version-script=${map})
    set_target_properties(${name} PROPERTIES LINK_DEPENDS ${map})
    add_library(${name}_static STATIC $<TARGET_OBJECTS:${name}_obj>)
  else()
    add_library(${name}_static ALIAS ${name})
  endif()

  target_include_directories(${name}_obj PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  cos_target_include_directories(${name} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

  # NOTE: the sources themselves build as C++26. may have ABI issues
  cos_target_compile_features(${name} PUBLIC cxx_std_23)
  # build-only flags
  target_link_libraries(${name}_obj PRIVATE $<BUILD_INTERFACE:project_warnings> $<BUILD_INTERFACE:project_options>)
  target_link_libraries(${name} PRIVATE $<BUILD_INTERFACE:project_options>)
  if(NOT type STREQUAL "STATIC_LIBRARY")
    target_link_libraries(${name}_static PRIVATE $<BUILD_INTERFACE:project_options>)
  endif()
endfunction()

# the real targets of <name>.
function(_cos_targets_of name out)
  set(targets ${name}_obj ${name})
  if(TARGET ${name}_static)
    get_target_property(aliased ${name}_static ALIASED_TARGET)
    if(NOT aliased)
      list(APPEND targets ${name}_static)
    endif()
  endif()
  set(${out} ${targets} PARENT_SCOPE)
endfunction()

function(cos_target_include_directories name)
  _cos_targets_of(${name} targets)
  foreach(target IN LISTS targets)
    target_include_directories(${target} ${ARGN})
  endforeach()
endfunction()

function(cos_target_compile_features name)
  _cos_targets_of(${name} targets)
  foreach(target IN LISTS targets)
    target_compile_features(${target} ${ARGN})
  endforeach()
endfunction()

# cos_target_link_libraries(<name> [PUBLIC <lib>...] [PRIVATE <lib>...])
# A <lib> that is a cos library links <lib>_static into <name>_static,
function(cos_target_link_libraries name)
  cmake_parse_arguments(ARG "" "" "PUBLIC;PRIVATE" ${ARGN})
  set(static_public)
  foreach(lib IN LISTS ARG_PUBLIC)
    if(TARGET ${lib}_static)
      list(APPEND static_public ${lib}_static)
    else()
      list(APPEND static_public ${lib})
    endif()
  endforeach()

  target_link_libraries(${name}_obj PUBLIC ${ARG_PUBLIC} PRIVATE ${ARG_PRIVATE})
  target_link_libraries(${name} PUBLIC ${ARG_PUBLIC} PRIVATE ${ARG_PRIVATE})
  _cos_targets_of(${name} targets)
  if(${name}_static IN_LIST targets)
    target_link_libraries(${name}_static PUBLIC ${static_public} PRIVATE ${ARG_PRIVATE})
  endif()
endfunction()
