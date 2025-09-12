function(set_project_warnings project_name)
    option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
    option(ENABLE_HARD_WARNINGS "Enable HARD warnings for static ananlyzer" OFF)

    set(MSVC_WARNINGS
        /W4 # Baseline reasonable warnings
        /w14242 # 'identifier': conversion from 'type1' to 'type1', possible loss of data
        /w14254 # 'operator': conversion from 'type1:field_bits' to 'type2:field_bits', possible loss of data
        /w14263 # 'function': member function does not override any base class virtual member function
        /w14265 # 'classname': class has virtual functions, but destructor is not virtual instances of this class may not
              # be destructed correctly
        /w14287 # 'operator': unsigned/negative constant mismatch
        /we4289 # nonstandard extension used: 'variable': loop control variable declared in the for-loop is used outside
              # the for-loop scope
        /w14296 # 'operator': expression is always 'boolean_value'
        /w14311 # 'variable': pointer truncation from 'type1' to 'type2'
        /w14545 # expression before comma evaluates to a function which is missing an argument list
        /w14546 # function call before comma missing argument list
        /w14547 # 'operator': operator before comma has no effect; expected operator with side-effect
        /w14549 # 'operator': operator before comma has no effect; did you intend 'operator'?
        /w14555 # expression has no effect; expected expression with side- effect
        /w14619 # pragma warning: there is no warning number 'number'
        /w14640 # Enable warning on thread un-safe static member initialization
        /w14826 # Conversion from 'type1' to 'type_2' is sign-extended. This may cause unexpected runtime behavior.
        /w14905 # wide string literal cast to 'LPSTR'
        /w14906 # string literal cast to 'LPWSTR'
        /w14928 # illegal copy-initialization; more than one user-defined conversion has been implicitly applied
        /permissive- # standards conformance mode for MSVC compiler.
    )

    set(CLANG_WARNINGS
        -Wall
        -Wextra # reasonable and standard
        -Wshadow # warn the user if a variable declaration shadows one from a parent context
        -Wnon-virtual-dtor # warn the user if a class with virtual functions has a non-virtual destructor. This helps
                         # catch hard to track down memory errors
        -Wold-style-cast # warn for c-style casts
        -Wcast-align # warn for potential performance problem casts
        -Wunused # warn on anything being unused
        -Woverloaded-virtual # warn if you overload (not override) a virtual function
        -Wpedantic # warn if non-standard C++ is used
        -Wconversion # warn on type conversions that may lose data
        -Wsign-conversion # warn on sign conversions
        -Wnull-dereference # warn if a null dereference is detected
        -Wdouble-promotion # warn if float is implicit promoted to double
        -Wformat=2 # warn on security issues around functions that format output (ie printf)
        -Wimplicit-fallthrough # warn on statements that fallthrough without an explicit annotation
    )

    if(WARNINGS_AS_ERRORS AND NOT ENABLE_HARD_WARNINGS)
        set(CLANG_WARNINGS ${CLANG_WARNINGS} -Werror)
        set(MSVC_WARNINGS ${MSVC_WARNINGS} /WX)
    endif()

    set(GCC_WARNINGS
        ${CLANG_WARNINGS}
        -Wno-deprecated
        -Wmisleading-indentation # warn if indentation implies blocks where blocks do not exist
        -Wduplicated-cond # warn if if / else chain has duplicated conditions
        -Wduplicated-branches # warn if if / else branches have duplicated code
        -Wlogical-op # warn about logical operations being used where bitwise were probably wanted
        -Wuseless-cast # warn if you perform a cast to the same type

        #-Wsuggest-attribute=cold
        #-Wsuggest-attribute=format
        -Wsuggest-attribute=malloc
        -Wsuggest-attribute=noreturn
        -Wsuggest-attribute=pure
        #-Wsuggest-final-methods
        #-Wsuggest-final-types
        -Wsuggest-override
        -Wdiv-by-zero
        -Wanalyzer-double-fclose
        -Wanalyzer-double-free
        -Wanalyzer-malloc-leak
        -Wanalyzer-use-after-free

        ## some more analyzer flags
        -Wnonnull
        -Wnonnull-compare
        -Wnormalized=nfkc
        -Wnull-dereference
        -Wopenacc-parallelism
        -Wopenmp-simd
        -Wpmf-conversions

        -Waggressive-loop-optimizations
        -Wanalyzer-tainted-allocation-size
        -Wanalyzer-use-of-uninitialized-value
        -Wanalyzer-use-of-pointer-in-stale-stack-frame
        -Wanalyzer-free-of-non-heap
        -Wanalyzer-mismatching-deallocation
        -Wanalyzer-null-dereference
        -Wanalyzer-possible-null-dereference
    )

    if(ENABLE_HARD_WARNINGS)
        set(CLANG_WARNINGS ${CLANG_WARNINGS}
            -Weverything
            -Wno-c++98-compat
            -Wno-c++98-compat-pedantic
            -Wno-c++11-compat
            -Wno-c++14-compat
            -Wno-documentation-unknown-command
            -Wno-reserved-identifier
        )

        set(GCC_WARNINGS
            ${GCC_WARNINGS}
            -fanalyzer
            -Wanalyzer-too-complex


            ## just for testing
            -Weffc++
            -Walloca
            -Walloc-zero
            -Wcannot-profile
            -Wcast-align
            -Wcast-align=strict
            -Wcast-function-type
            -Wcast-qual
            -Wcatch-value=3
            -Wchar-subscripts
            -Wclass-conversion
            -Wclass-memaccess
            -Wclobbered
            -Wcomma-subscript
            -Wcomment
            -Wcomments
            -Wconditionally-supported
            -Wconversion
            -Wconversion-null
            -Wcoverage-invalid-line-number
            -Wcoverage-mismatch
            -Wcpp
            -Wctad-maybe-unsupported
            -Wctor-dtor-privacy
            -Wdangling-else
            -Wdangling-pointer=2
            -Wdate-time
            -Wdelete-incomplete
            -Wdelete-non-virtual-dtor
            -Wframe-address
            -Wfree-nonheap-object
            -Whsa
            -Wlto-type-mismatch
            -Wno-namespaces
            -Wnarrowing
            -Wnoexcept
            -Wnoexcept-type
            -Wnon-template-friend
            -Wnon-virtual-dtor
            -Wodr
            -Wold-style-cast
            -Woverflow
            -Woverlength-strings
            -Woverloaded-virtual
            -Wpacked
            -Wpacked-bitfield-compat
            -Wpacked-not-aligned
            -Wno-padded
            -Wparentheses
            -Wpessimizing-move
            -Wplacement-new=2
            -Wpragmas
            -Wprio-ctor-dtor
            -Wpsabi
            -Wrange-loop-construct
            -Wredundant-decls
            -Wredundant-move
            -Wredundant-tags
            -Wregister
            -Wscalar-storage-order
            -Wsequence-point
            -Wshadow=compatible-local
            -Wshadow=global
            -Wshadow=local
            -Wstack-protector
            -Wsync-nand
            -Wsynth
            -Wtsan
            -Wtype-limits
            -Wundef
            -Wuninitialized
            -Wunknown-pragmas
            -Wunreachable-code
            -Wunsafe-loop-optimizations
            -Wuse-after-free=3
            -Wvector-operation-performance
            -Wvexing-parse
            -Wvirtual-inheritance
            -Wvirtual-move-assign
            -Wvla
            -Wvla-parameter
            -Wvolatile
            -Wvolatile-register-var
            -Wwrite-strings
            -Wzero-as-null-pointer-constant
            -Wzero-length-bounds

            -Wanalyzer-exposure-through-output-file
            -Wanalyzer-file-leak
            -Wanalyzer-null-argument
            -Wanalyzer-possible-null-argument
            -Wanalyzer-shift-count-negative
            -Wanalyzer-shift-count-overflow
            -Wanalyzer-stale-setjmp-buffer
            -Wanalyzer-unsafe-call-within-signal-handler
            -Wanalyzer-write-to-const
            -Wanalyzer-write-to-string-literal
        )
    endif()

    if(MSVC)
        set(PROJECT_WARNINGS ${MSVC_WARNINGS})
    elseif(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
        set(PROJECT_WARNINGS ${CLANG_WARNINGS})
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(PROJECT_WARNINGS ${GCC_WARNINGS})
    else()
        message(AUTHOR_WARNING "No compiler warnings set for '${CMAKE_CXX_COMPILER_ID}' compiler.")
    endif()

    target_compile_options(${project_name} INTERFACE ${PROJECT_WARNINGS})
endfunction()

# FIXME(vnepogodin,luigidematteis): temp hack for ftxui
# Function to suppress warnings for FTXUI library targets
# This is a temporary workaround until FTXUI is updated to fix these warnings upstream
function(suppress_ftxui_warnings)
    # FTXUI has three separate library targets:
    # - screen: Low-level terminal/screen handling
    # - dom: Document Object Model for UI elements and layout
    # - component: Interactive UI components and event handling
    #
    # Suppressed warnings:
    # - Wno-deprecated: General deprecated features
    # - Wno-float-equal: Direct floating point comparisons
    # - Wno-deprecated-declarations: Using deprecated functions/types
    # - Wno-suggest-attribute=const: GCC 15+ suggests const over pure attribute
    # - Wno-suggest-final-methods/types: GCC 15+ optimization suggestions for virtual functions
    # - Wno-deprecated-literal-operator: C++23 deprecated spacing in operator"" _z()
    # - Wno-error: Don't treat remaining warnings as errors for these targets

    # Common warnings to suppress for all compilers except MSVC
    set(COMMON_SUPPRESSIONS
        -Wno-deprecated
        -Wno-float-equal
        -Wno-error
        -Wno-deprecated-literal-operator
    )

    # GCC-specific suppressions
    set(GCC_SUPPRESSIONS
        -Wno-deprecated-declarations
        -Wno-suggest-attribute=const
        -Wno-suggest-final-methods
        -Wno-suggest-final-types
        -Wno-error
    )

    # Apply suppressions to all FTXUI targets
    foreach(target screen dom component)
        if(TARGET ${target})
            target_compile_options(${target} PRIVATE
                $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:${COMMON_SUPPRESSIONS}>
                $<$<CXX_COMPILER_ID:GNU>:${GCC_SUPPRESSIONS}>
            )
        endif()
    endforeach()
endfunction()
