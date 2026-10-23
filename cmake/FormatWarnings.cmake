include_guard(GLOBAL)

# Deliberately not -Wall/-Wextra, -Wformat=2, or broad -Werror.
# Keep controls and production on this one explicit policy.
set(EQEMU_FORMAT_WARNING_OPTIONS -Wformat -Werror=format)
set(EQEMU_FORMAT_WARNINGS_SUPPORTED FALSE)
if(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$"
   AND NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(EQEMU_FORMAT_WARNINGS_SUPPORTED TRUE)
endif()

function(eqemu_apply_format_warnings target)
    if(EQEMU_FORMAT_WARNINGS_SUPPORTED)
        target_compile_options(${target} PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:${EQEMU_FORMAT_WARNING_OPTIONS}>")
        message(STATUS "Format gate: ${target} C++ sources: ${EQEMU_FORMAT_WARNING_OPTIONS}")
    endif()
endfunction()

# CMake regexes are byte-oriented: GCC's UTF-8 quotes are not single
# characters to '.'. Normalize only those delimiters, not the evidence text.
function(eqemu_format_control_rejected compiled output result)
    string(REPLACE "‘" "'" normalized "${output}")
    string(REPLACE "’" "'" normalized "${normalized}")
    set(rejected FALSE)
    if(NOT compiled AND normalized MATCHES "-Werror(=format|,-Wformat)"
       AND normalized MATCHES "expects argument of type .double.|format specifies type .double."
       AND normalized MATCHES "int")
        set(rejected TRUE)
    endif()
    set(${result} "${rejected}" PARENT_SCOPE)
endfunction()

function(eqemu_check_format_warnings)
    if(NOT EQEMU_FORMAT_WARNINGS_SUPPORTED)
        message(STATUS "Format gate: unsupported compiler/frontend; no policy or controls applied")
        return()
    endif()

    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/format_controls/diagnostics.cmake")

    # Compile only: no executable, linker, or runtime requirement. Rebuild on
    # every configure, including after compiler/options/control-source changes.
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    set(control_root "${CMAKE_BINARY_DIR}/CMakeFiles/eqemu-format-controls")
    file(REMOVE_RECURSE "${control_root}")
    file(MAKE_DIRECTORY "${control_root}")
    foreach(control IN ITEMS safe unsafe)
        unset(compiled CACHE)
        unset(compiled)
        try_compile(compiled "${control_root}/${control}"
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/format_controls/${control}.cpp"
            CMAKE_FLAGS
                "-DCMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}"
                "-DCMAKE_CXX_STANDARD_REQUIRED=ON"
                "-DCMAKE_CXX_EXTENSIONS=OFF"
            COMPILE_DEFINITIONS ${EQEMU_FORMAT_WARNING_OPTIONS}
            OUTPUT_VARIABLE output)
        file(WRITE "${control_root}/${control}.log" "${output}")
        if(control STREQUAL "safe")
            if(NOT compiled)
                message(FATAL_ERROR "Format gate: explicit-double control failed:\n${output}")
            endif()
            message(STATUS "Format gate: explicit-double control compiled")
        else()
            # A missing header, unsupported switch, or unrelated compiler error
            # must NOT count as successful rejection of the regression.
            eqemu_format_control_rejected("${compiled}" "${output}" rejected)
            if(NOT rejected)
                message(FATAL_ERROR "Format gate: unsafe control did not fail for the expected int/double format diagnostic:\n${output}")
            endif()
            string(REGEX MATCH "[^\n]*(expects argument of type|format specifies type)[^\n]*" diagnostic "${output}")
            message(STATUS "Format gate: unsafe int control rejected: ${diagnostic}")
        endif()
        unset(compiled CACHE)
    endforeach()
endfunction()
