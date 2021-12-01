option(ENABLE_CPPCHECK "Enable cppcheck [default: OFF]" OFF)
option(ENABLE_CLANG_TIDY "Enable clang-tidy [default: OFF]" OFF)
option(ENABLE_INCLUDE_WHAT_YOU_USE "Enable include-what-you-use [default: OFF]" OFF)

if(ENABLE_CPPCHECK)
    find_program(CPPCHECK_EXE
        NAMES cppcheck
        DOC "Path to cppcheck executable")
    if(NOT CPPCHECK_EXE)
        message(STATUS "[cppcheck] Not found.")
    else()
        message(STATUS "[cppcheck] found: ${CPPCHECK_EXE}")
        set(CMAKE_CXX_CPPCHECK
            "${CPPCHECK_EXE}"
            --suppress=missingInclude
            --enable=all
            --inline-suppr
            --inconclusive)
        if(WARNINGS_AS_ERRORS)
            list(APPEND CMAKE_CXX_CPPCHECK --error-exitcode=2)
        endif()
    endif()
else()
    message(STATUS "[cppcheck] Disabled.")
endif()

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE
        NAMES clang-tidy-9 clang-tidy-8 clang-tidy-7 clang-tidy
        DOC "Path to clang-tidy executable")
    if(NOT CLANG_TIDY_EXE)
        message(STATUS "[clang-tidy] Not found.")
    else()
        message(STATUS "[clang-tidy] found: ${CLANG_TIDY_EXE}")
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
    endif()
else()
    message(STATUS "[clang-tidy] Disabled.")
endif()

if(ENABLE_INCLUDE_WHAT_YOU_USE)
    find_program(IWYU_EXE
        NAMES include-what-you-use
        DOC "Path to include-what-you-use executable")
    if(NOT IWYU_EXE)
        message(STATUS "[iwyu] Not found.")
    else()
        message(STATUS "[iwyu] found: ${IWYU_EXE}")
        set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE "${IWYU_EXE}")
    endif()
else()
    message(STATUS "[iwyu] Disabled.")
endif()
