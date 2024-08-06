#############################################################################
# doctest library with the main function to speed up build
#############################################################################

add_library(doctest_main OBJECT unit.cpp)
target_compile_features(doctest_main PUBLIC cxx_std_23)
target_include_directories(doctest_main PRIVATE ${CMAKE_BINARY_DIR}/include ${CMAKE_CURRENT_DIR})
target_link_libraries(doctest_main PRIVATE doctest::doctest)

#############################################################################
# one executable for each unit test file
#############################################################################

file(GLOB files unit-*.cpp)

set(GUCC_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(GUCC_TOP_DIR "${CMAKE_SOURCE_DIR}")

foreach(file ${files})
    get_filename_component(file_basename ${file} NAME_WE)
    string(REGEX REPLACE "unit-([^$]+)" "test-\\1" testcase ${file_basename})

    add_executable(${testcase} $<TARGET_OBJECTS:doctest_main> ${file})
    target_compile_definitions(${testcase} PRIVATE DOCTEST_CONFIG_SUPER_FAST_ASSERTS)
    target_compile_options(${testcase} PRIVATE
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-deprecated;-Wno-float-equal>
        $<$<CXX_COMPILER_ID:GNU>:-Wno-deprecated-declarations>
    )
    add_definitions(-DGUCC_TEST_DIR="${GUCC_TEST_DIR}")
    add_definitions(-DGUCC_TOP_DIR="${GUCC_TOP_DIR}")
    target_link_libraries(${testcase} PRIVATE project_warnings project_options gucc::gucc doctest::doctest)

    add_test(NAME "${testcase}"
        COMMAND ${testcase} ${DOCTEST_TEST_FILTER} --no-skip
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endforeach()
