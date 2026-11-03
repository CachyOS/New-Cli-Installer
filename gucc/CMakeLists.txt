cmake_minimum_required(VERSION 3.25)

##
## PROJECT
## name and version
##
project(gucc
        VERSION 0.0.1
        LANGUAGES CXX)


cos_add_library(${PROJECT_NAME} EXPORT_MACRO GUCC EXPORT_HEADER gucc/export.hpp NAMESPACE gucc SOURCES
   src/error.cpp
   src/log_redact.cpp
   src/io_utils.cpp
   src/conf.cpp
   src/string_utils.cpp
   src/file_utils.cpp
   src/fs_utils.cpp
   src/cpu.cpp
   src/memory.cpp
   src/pacmanconf_repo.cpp
   src/repos.cpp
   src/initcpio.cpp
   src/block_devices.cpp
   src/crypto_detection.cpp
   src/partition_config.cpp
   src/partitioning.cpp
   src/swap.cpp
   src/luks.cpp
   src/zfs.cpp
   src/zfs_query.cpp
   src/btrfs.cpp
   src/btrfs_query.cpp
   src/system_query.cpp
   src/user.cpp
   src/locale.cpp
   src/fstab.cpp
   src/crypttab.cpp
   src/bootloader.cpp
   src/systemd_services.cpp
   src/autologin.cpp
   src/mtab.cpp
   src/umount_partitions.cpp
   src/mount_partitions.cpp
   src/hwclock.cpp
   src/machine_id.cpp
   src/keyboard.cpp
   src/display_manager.cpp
   src/plymouth.cpp
   src/network.cpp
   src/luks_swap.cpp
   src/package_profiles.cpp
   src/server_profiles.cpp
   src/fetch_file.cpp
   src/package_list.cpp
   src/kernel_params.cpp
   src/chwd.cpp
   src/timezone.cpp
   src/lvm.cpp
   src/systemd_repart.cpp
   src/systemd_homed.cpp
   src/install.cpp
   src/firewall.cpp
   src/mirrors.cpp
   src/process.cpp
   src/kmod.cpp
   src/pacman.cpp
   src/logger.cpp
   )

add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

file(GLOB GUCC_PUBLIC_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/include/gucc/*.hpp)
target_sources(${PROJECT_NAME} PUBLIC FILE_SET HEADERS BASE_DIRS include ${CMAKE_CURRENT_BINARY_DIR}/include
   FILES ${GUCC_PUBLIC_HEADERS} ${CMAKE_CURRENT_BINARY_DIR}/include/gucc/export.hpp)

# public headers use fmt templates
cos_target_link_libraries(${PROJECT_NAME}
   PUBLIC $<BUILD_INTERFACE:fmt::fmt>
   PRIVATE spdlog::spdlog tomlplusplus::tomlplusplus cpr::cpr)
target_include_directories(${PROJECT_NAME}_obj PRIVATE ${RAPIDJSON_INCLUDE_DIR})

# runner stubs for tests
add_library(gucc_testing INTERFACE)
add_library(gucc::testing ALIAS gucc_testing)
target_include_directories(gucc_testing INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/testing/include)
target_link_libraries(gucc_testing INTERFACE gucc_static)

option(GUCC_BUILD_TOOLS "Build GUCC CLI tools" OFF)
if(GUCC_BUILD_TOOLS)
   add_executable(gucc-disk-query tools/gucc-disk-query.cpp)
   target_link_libraries(gucc-disk-query PRIVATE project_warnings project_options gucc::gucc spdlog::spdlog fmt::fmt)

   add_executable(gucc-btrfs-query tools/gucc-btrfs-query.cpp)
   target_link_libraries(gucc-btrfs-query PRIVATE project_warnings project_options gucc::gucc spdlog::spdlog fmt::fmt)

   add_executable(gucc-zfs-query tools/gucc-zfs-query.cpp)
   target_link_libraries(gucc-zfs-query PRIVATE project_warnings project_options gucc::gucc spdlog::spdlog fmt::fmt)

   install(
      TARGETS gucc-disk-query gucc-btrfs-query gucc-zfs-query
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
   )
endif()

if(COS_INSTALLER_BUILD_TESTS)
   add_subdirectory(tests)
endif()
