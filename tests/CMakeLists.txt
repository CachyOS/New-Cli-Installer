list(APPEND test_SOURCES
        ${CMAKE_SOURCE_DIR}/src/config.cpp
        ${CMAKE_SOURCE_DIR}/src/utils.cpp
        ${CMAKE_SOURCE_DIR}/src/disk.cpp
        ${CMAKE_SOURCE_DIR}/src/drivers.cpp
        ${CMAKE_SOURCE_DIR}/src/widgets.cpp
        ${CMAKE_SOURCE_DIR}/src/follow_process_log.cpp
        ${CMAKE_SOURCE_DIR}/src/crypto.cpp
        ${CMAKE_SOURCE_DIR}/src/misc.cpp
        ${CMAKE_SOURCE_DIR}/src/simple_tui.cpp
        ${CMAKE_SOURCE_DIR}/src/tui.cpp
)

add_library(test_libreq STATIC ${test_SOURCES})
target_include_directories(test_libreq PRIVATE ${CMAKE_BINARY_DIR}/include ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_DIR})
target_link_libraries(test_libreq PRIVATE project_warnings project_options spdlog::spdlog fmt::fmt ftxui::component cpr::cpr range-v3::range-v3 ctre::ctre)

#############################################################################
# one executable for each unit test file
#############################################################################

file(GLOB files unit-*.cpp)

foreach(file ${files})
    get_filename_component(file_basename ${file} NAME_WE)
    string(REGEX REPLACE "unit-([^$]+)" "test-\\1" testcase ${file_basename})

    add_executable(${testcase} ${file})
    target_compile_options(${testcase} PRIVATE
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-deprecated;-Wno-float-equal>
        $<$<CXX_COMPILER_ID:GNU>:-Wno-deprecated-declarations>
    )
    target_include_directories(${testcase} PRIVATE ${CMAKE_BINARY_DIR}/include ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_DIR})
    target_link_libraries(${testcase} PRIVATE project_warnings project_options test_libreq spdlog::spdlog fmt::fmt ftxui::component cpr::cpr range-v3::range-v3 ctre::ctre)
endforeach()
