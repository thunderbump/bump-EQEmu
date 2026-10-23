# Exercise the production decision on recorded diagnostic shapes on every
# test-enabled configure; no compiler, executable or CTest invocation needed.
set(gcc_ascii "error: format '%f' expects argument of type 'double', but argument 4 has type 'int' [-Werror=format=]")
set(gcc_utf8 "error: format ‘%f’ expects argument of type ‘double’, but argument 4 has type ‘int’ [-Werror=format=]")
set(clang_ascii "error: format specifies type 'double' but the argument has type 'int' [-Werror,-Wformat]")
set(clang_utf8 "error: format specifies type ‘double’ but the argument has type ‘int’ [-Werror,-Wformat]")
foreach(sample IN ITEMS gcc_ascii gcc_utf8 clang_ascii clang_utf8)
    eqemu_format_control_rejected(FALSE "${${sample}}" rejected)
    if(NOT rejected)
        message(FATAL_ERROR "Format gate matcher regression: rejected valid ${sample} diagnostic")
    endif()
    eqemu_format_control_rejected(TRUE "${${sample}}" rejected)
    if(rejected)
        message(FATAL_ERROR "Format gate matcher regression: accepted successful compilation")
    endif()
endforeach()

# Neither an unrelated failure nor partial diagnostic evidence qualifies.
foreach(sample IN ITEMS
    "fatal error: missing header for int/double control"
    "error: format ‘%f’ expects argument of type ‘double’, but argument 4 has type ‘int’"
    "error: format ‘%f’ expects argument of type ‘double’, but argument 4 has type ‘long’ [-Werror=format=]"
    "error: format ‘%d’ expects argument of type ‘int’, but argument 4 has type ‘double’ [-Werror=format=]"
    "error: format ‘%f’ expects argument of type ‘double’, but argument 4 has type ‘int’ [-Werror=unused]")
    eqemu_format_control_rejected(FALSE "${sample}" rejected)
    if(rejected)
        message(FATAL_ERROR "Format gate matcher regression: accepted unrelated/partial diagnostic")
    endif()
endforeach()
