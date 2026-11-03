include_guard(GLOBAL)

include(CMakePackageConfigHelpers)

set(_cos_libs gucc cachyos-installer-lib)
set_target_properties(gucc PROPERTIES EXPORT_NAME gucc)
set_target_properties(cachyos-installer-lib PROPERTIES EXPORT_NAME installer)

get_target_property(_cos_lib_type gucc TYPE)
if(NOT COS_INSTALL_DEVEL)
  if(_cos_lib_type STREQUAL "SHARED_LIBRARY")
    install(TARGETS ${_cos_libs}
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} NAMELINK_SKIP)
  endif()
  return()
endif()

set(_cos_cmake_dir ${CMAKE_INSTALL_LIBDIR}/cmake/cachyos-installer)

install(TARGETS ${_cos_libs}
  EXPORT cachyos-installer-targets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT cachyos-installer-targets
  NAMESPACE cachyos::
  DESTINATION ${_cos_cmake_dir})

configure_package_config_file(
  ${CMAKE_CURRENT_LIST_DIR}/cachyos-installerConfig.cmake.in
  ${CMAKE_BINARY_DIR}/cachyos-installerConfig.cmake
  INSTALL_DESTINATION ${_cos_cmake_dir})

# a minor bump may break the API
write_basic_package_version_file(
  ${CMAKE_BINARY_DIR}/cachyos-installerConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMinorVersion)
install(FILES
  ${CMAKE_BINARY_DIR}/cachyos-installerConfig.cmake
  ${CMAKE_BINARY_DIR}/cachyos-installerConfigVersion.cmake
  DESTINATION ${_cos_cmake_dir})
