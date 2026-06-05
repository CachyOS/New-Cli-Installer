#############################################################################
# doctest library with the main function to speed up build
#############################################################################

add_library(installer_lib_doctest_main OBJECT unit.cpp)
target_compile_features(installer_lib_doctest_main PUBLIC cxx_std_23)
target_include_directories(installer_lib_doctest_main PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(installer_lib_doctest_main PRIVATE doctest::doctest)

#############################################################################
# one executable for each unit test file
#############################################################################

file(GLOB files unit-*.cpp)

foreach(file ${files})
    get_filename_component(file_basename ${file} NAME_WE)
    string(REGEX REPLACE "unit-([^$]+)" "test-\\1" testcase ${file_basename})

    add_executable(${testcase} $<TARGET_OBJECTS:installer_lib_doctest_main> ${file})
    target_compile_definitions(${testcase} PRIVATE DOCTEST_CONFIG_SUPER_FAST_ASSERTS)
    target_compile_options(${testcase} PRIVATE
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-deprecated;-Wno-float-equal>
        $<$<CXX_COMPILER_ID:GNU>:-Wno-deprecated-declarations>
    )
    target_link_libraries(${testcase} PRIVATE
        project_warnings project_options
        cachyos::installer-lib gucc::gucc
        doctest::doctest spdlog::spdlog fmt::fmt cpr::cpr)

    add_test(NAME "${testcase}"
        COMMAND ${testcase} ${DOCTEST_TEST_FILTER} --no-skip
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endforeach()
