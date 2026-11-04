include_guard(GLOBAL)

option(EQEMU_SANITIZERS "Instrument project-owned native targets with ASan and UBSan" OFF)

if(EQEMU_SANITIZERS)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux"
            OR NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$"
            OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        message(FATAL_ERROR "EQEMU_SANITIZERS requires Linux GCC or Clang with a GNU-style frontend")
    endif()
endif()

# Walk declared targets, including project object libraries. Do not instrument
# separately built dependencies or the vendored utility framework.
function(eqemu_apply_sanitizers directory)
    if(NOT EQEMU_SANITIZERS)
        return()
    endif()

    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        get_target_property(type "${target}" TYPE)
        if(type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
            target_compile_options("${target}" PRIVATE
                -fsanitize=address,undefined -fno-sanitize-recover=all
                -g -O1 -fno-omit-frame-pointer -fno-optimize-sibling-calls)
            target_link_options("${target}" PRIVATE
                -fsanitize=address,undefined -fno-sanitize-recover=all)
            if(target STREQUAL "lua_zone")
                # Keep each diagnostic Lua compilation to one binding source.
                set_property(TARGET "${target}" PROPERTY UNITY_BUILD OFF)
            endif()
        endif()
    endforeach()

    get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        if(NOT child STREQUAL "${CMAKE_SOURCE_DIR}/libs"
                AND NOT child STREQUAL "${CMAKE_SOURCE_DIR}/submodules"
                AND NOT child STREQUAL "${CMAKE_SOURCE_DIR}/tests/cppunit")
            eqemu_apply_sanitizers("${child}")
        endif()
    endforeach()
endfunction()
