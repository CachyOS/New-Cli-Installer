list(APPEND test_SOURCES
        ${CMAKE_SOURCE_DIR}/src/installer_state.cpp
        ${CMAKE_SOURCE_DIR}/src/utils.cpp
        ${CMAKE_SOURCE_DIR}/src/disk.cpp
        ${CMAKE_SOURCE_DIR}/src/drivers.cpp
        ${CMAKE_SOURCE_DIR}/src/widgets.cpp
        ${CMAKE_SOURCE_DIR}/src/follow_process_log.cpp
        ${CMAKE_SOURCE_DIR}/src/install_observers.cpp
        ${CMAKE_SOURCE_DIR}/src/crypto.cpp
        ${CMAKE_SOURCE_DIR}/src/misc.cpp
        ${CMAKE_SOURCE_DIR}/src/simple_tui.cpp
        ${CMAKE_SOURCE_DIR}/src/tui.cpp
)

add_library(test_libreq STATIC ${test_SOURCES})
target_link_libraries(test_libreq PUBLIC tui_includes cachyos-installer-lib_static PRIVATE project_warnings project_options spdlog::spdlog fmt::fmt ftxui::component ctre::ctre tomlplusplus::tomlplusplus)
if(NOT ENABLE_DEVENV)
  target_link_libraries(test_libreq PRIVATE cpr::cpr)
endif()

#############################################################################
# one executable for each unit test file
#############################################################################

file(GLOB files unit-*.cpp)

# Interactive demos
set(interactive_tests test-exec-interactive test-with-restored-io test-proccess-tailing)

foreach(file ${files})
    get_filename_component(file_basename ${file} NAME_WE)
    string(REGEX REPLACE "unit-([^$]+)" "test-\\1" testcase ${file_basename})

    add_executable(${testcase} ${file})
    target_compile_options(${testcase} PRIVATE
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-deprecated;-Wno-float-equal>
        $<$<CXX_COMPILER_ID:GNU>:-Wno-deprecated-declarations>
    )
    target_include_directories(${testcase} PRIVATE ${CMAKE_SOURCE_DIR}/gucc/tests)
    target_link_libraries(${testcase} PRIVATE project_warnings project_options test_libreq gucc::testing spdlog::spdlog fmt::fmt ftxui::component ctre::ctre tomlplusplus::tomlplusplus doctest::doctest)
    if(NOT ENABLE_DEVENV)
      target_link_libraries(${testcase} PRIVATE cpr::cpr)
    endif()

    if(NOT testcase IN_LIST interactive_tests)
      add_test(NAME "${testcase}"
          COMMAND ${testcase} ${DOCTEST_TEST_FILTER} --no-skip
          WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      )
    endif()
endforeach()
