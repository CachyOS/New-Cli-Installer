function(set_project_warnings project_name)
    option(WARNINGS_AS_ERRORS "Treat compiler warnings as error" ON)

    set(MSVC_WARNINGS
        /W4 # Base
        /w14242 # Conversion
        /w14254 # Operator convers.
        /w14263 # Func member doesn't override
        /w14265 # class has vfuncs, but destructor is not
        /w14287 # unsigned/negative constant mismatch
        /we4289 # nonstandard extension used: loop control var
        /w14296 # expression is always 'boolean_value'
        /w14311 # pointer trunc from one tipe to another
        /w14545 # expression before comma evaluates to a function which missign an argument list

        /w14546 # function call before comma missing argument list
        /w14547 # operator before comma has no effect; expected operator with side-effect
        /w14549 # operator before comma has no effect; did you intend operator?

        /w14555 # expresion has no effect; expected expression with side-effect
        /w14619 # pragma warning
        /w14640 # Enable warning on thread; static member

        /w14826 # Conversion from one tipe to another is sign-extended cause unexpected runtime behavior.
        /w14928 # illegal copy-initialization; more than user-defined.
        /X
        /constexpr
    )

    set(CLANG_WARNINGS
        -Wall
        -Wextra # standard
        -Wshadow

        -Wnon-virtual-dtor

        -Wold-style-cast # c-style cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual

        -Wpedantic # non-standard C++
        -Wconversion # type conversion that may lose data
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion # float to double

        -Wformat=2
    )

    if(WARNINGS_AS_ERRORS)
        set(CLANG_WARNINGS ${CLANG_WARNINGS} -Werror)
        set(MSVC_WARNINGS ${MSVC_WARNINGS} /WX)
    endif()

    set(GCC_WARNINGS
        ${CLANG_WARNINGS}
        -Wmisleading-indentation

        -Wduplicated-cond
        -Wduplicated-branches
        -Wlogical-op

        -Wuseless-cast
    )

    if(MSVC)
        set(PROJECT_WARNINGS ${MSVC_WARNINGS})
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(PROJECT_WARNINGS ${CLANG_WARNINGS})
    else()
        set(PROJECT_WARNINGS ${GCC_WARNINGS})
    endif()

    target_compile_options(${project_name} INTERFACE ${PROJECT_WARNINGS})
endfunction()
