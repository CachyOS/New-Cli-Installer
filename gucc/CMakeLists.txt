cmake_minimum_required(VERSION 3.6)

##
## PROJECT
## name and version
##
project(gucc
        VERSION 0.0.1
        LANGUAGES CXX)


set(GUCC_LOGGER_FILES)
if(NOT COS_BUILD_STATIC)
   set(GUCC_LOGGER_FILES src/logger.cpp include/gucc/logger.hpp)
endif()

add_library(${PROJECT_NAME} #SHARED
   #src/utils.cpp src/utils.hpp
   src/error.cpp include/gucc/error.hpp
   src/io_utils.cpp include/gucc/io_utils.hpp
   src/string_utils.cpp include/gucc/string_utils.hpp
   src/file_utils.cpp include/gucc/file_utils.hpp
   src/fs_utils.cpp include/gucc/fs_utils.hpp
   src/cpu.cpp include/gucc/cpu.hpp
   src/pacmanconf_repo.cpp include/gucc/pacmanconf_repo.hpp
   src/repos.cpp include/gucc/repos.hpp
   src/initcpio.cpp include/gucc/initcpio.hpp
   src/block_devices.cpp include/gucc/block_devices.hpp
   src/crypto_detection.cpp include/gucc/crypto_detection.hpp
   src/partition_config.cpp include/gucc/partition_config.hpp
   src/partitioning.cpp include/gucc/partitioning.hpp
   src/swap.cpp include/gucc/swap.hpp
   src/luks.cpp include/gucc/luks.hpp
   src/zfs.cpp include/gucc/zfs.hpp
   src/zfs_query.cpp include/gucc/zfs_query.hpp
   src/btrfs.cpp include/gucc/btrfs.hpp
   src/btrfs_query.cpp include/gucc/btrfs_query.hpp
   src/system_query.cpp include/gucc/system_query.hpp
   src/user.cpp include/gucc/user.hpp
   src/locale.cpp include/gucc/locale.hpp
   src/fstab.cpp include/gucc/fstab.hpp
   src/crypttab.cpp include/gucc/crypttab.hpp
   src/bootloader.cpp include/gucc/bootloader.hpp
   src/systemd_services.cpp include/gucc/systemd_services.hpp
   src/autologin.cpp include/gucc/autologin.hpp
   src/mtab.cpp include/gucc/mtab.hpp
   src/umount_partitions.cpp include/gucc/umount_partitions.hpp
   src/mount_partitions.cpp include/gucc/mount_partitions.hpp
   src/hwclock.cpp include/gucc/hwclock.hpp
   src/machine_id.cpp include/gucc/machine_id.hpp
   src/keyboard.cpp include/gucc/keyboard.hpp
   src/display_manager.cpp include/gucc/display_manager.hpp
   src/plymouth.cpp include/gucc/plymouth.hpp
   src/network.cpp include/gucc/network.hpp
   src/luks_swap.cpp include/gucc/luks_swap.hpp
   src/package_profiles.cpp include/gucc/package_profiles.hpp
   src/fetch_file.cpp include/gucc/fetch_file.hpp
   src/package_list.cpp include/gucc/package_list.hpp
   src/kernel_params.cpp include/gucc/kernel_params.hpp
   src/chwd.cpp include/gucc/chwd.hpp
   src/timezone.cpp include/gucc/timezone.hpp
   src/lvm.cpp include/gucc/lvm.hpp
   src/systemd_repart.cpp include/gucc/systemd_repart.hpp
   src/systemd_homed.cpp include/gucc/systemd_homed.hpp
   src/install.cpp include/gucc/install.hpp
   src/subprocess.cpp include/gucc/subprocess.hpp
   src/log_redact.cpp
   ${GUCC_LOGGER_FILES}
   #src/disk.cpp src/disk.hpp
   )

add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_DIR}/include)
target_link_libraries(${PROJECT_NAME} PUBLIC project_warnings project_options spdlog::spdlog fmt::fmt tomlplusplus::tomlplusplus cpr::cpr)

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
