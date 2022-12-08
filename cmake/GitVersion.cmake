execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE VERSION_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

# Get current date
string(TIMESTAMP BUILD_TIMESTAMP "%Y%m%d")

# Check whether we got any revision (which isn't always the case, e.g.
# when someone downloaded a zip file from Github instead of a checkout)
if("${VERSION_COMMIT_HASH}" STREQUAL "")
  set(GIT_VERSION "${CMAKE_PROJECT_VERSION}-${BUILD_TIMESTAMP}")
else()
  set(GIT_VERSION "${CMAKE_PROJECT_VERSION}-${VERSION_COMMIT_HASH}-${BUILD_TIMESTAMP}")
endif()
