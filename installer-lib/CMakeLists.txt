cmake_minimum_required(VERSION 3.25)

##
## PROJECT
## name and version
##
project(cachyos-installer-lib
        VERSION 0.1.0
        LANGUAGES CXX)

cos_add_library(${PROJECT_NAME} EXPORT_MACRO CACHYOS_INSTALLER EXPORT_HEADER cachyos/export.hpp NAMESPACE cachyos SOURCES
   src/logging.cpp
   src/system.cpp
   src/requirements.cpp
   src/installer_config.cpp
   src/headless_plan.cpp
   src/config.cpp
   src/disk.cpp
   src/packages.cpp
   src/bootloader.cpp
   src/crypto.cpp
   src/validation.cpp
   src/validators.cpp
   src/installer_data.cpp
   src/session.cpp
   src/run_from_config.cpp
   src/orchestrator.cpp
   src/partition_planner.cpp
   src/steps/umount.cpp
   src/steps/partition.cpp
   src/steps/fstab.cpp
   src/steps/luks_keyfile.cpp
   src/steps/encrypt_swap.cpp
   src/steps/system_settings.cpp
   src/steps/users.cpp
   src/steps/base.cpp
   src/steps/machine_id.cpp
   src/steps/desktop.cpp
   src/steps/desktop_configure.cpp
   src/steps/server_packages.cpp
   src/steps/ssh_keys.cpp
   src/steps/server_firewall.cpp
   src/steps/autologin.cpp
   src/steps/chwd.cpp
   src/steps/network_carryover.cpp
   src/steps/initramfs.cpp
   src/steps/bootloader.cpp
   src/steps/detect_crypto.cpp
   src/steps/enable_services.cpp
   src/steps/btrfs_snapshot.cpp
   src/steps/final_validation.cpp
   src/steps/post_install.cpp
   src/steps/cleanup.cpp
   )

add_library(cachyos::installer-lib ALIAS ${PROJECT_NAME})

file(GLOB INSTALLER_PUBLIC_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/include/cachyos/*.hpp)
target_sources(${PROJECT_NAME} PUBLIC FILE_SET HEADERS BASE_DIRS include ${CMAKE_CURRENT_BINARY_DIR}/include
   FILES ${INSTALLER_PUBLIC_HEADERS} ${CMAKE_CURRENT_BINARY_DIR}/include/cachyos/export.hpp)

cos_target_link_libraries(${PROJECT_NAME}
   PUBLIC gucc
   PRIVATE spdlog::spdlog fmt::fmt cpr::cpr ctre::ctre)
target_include_directories(${PROJECT_NAME}_obj PRIVATE ${RAPIDJSON_INCLUDE_DIR})

if(COS_INSTALLER_BUILD_TESTS)
   add_subdirectory(tests)
endif()
