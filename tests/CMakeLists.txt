cmake_minimum_required(VERSION 3.20.0)

add_subdirectory(cppunit)

set(tests_sources
	main.cpp
)

set(tests_headers
	actor_runtime_test.h
	suite_runner.h
	atobool_test.h
	data_verification_test.h
	fixed_memory_test.h
	fixed_memory_variable_test.h
	hextoi_32_64_test.h
	ipc_mutex_test.h
	memory_mapped_file_test.h
	string_util_test.h
	skills_util_test.h
	task_state_test.h
)

add_executable(tests ${tests_sources} ${tests_headers})

# Explicit target for result-propagation controls; it is not installed with the server.
add_executable(tests_runner_controls EXCLUDE_FROM_ALL runner_controls.cpp suite_runner.h)
target_link_libraries(tests_runner_controls cppunit)

# Small, opt-in lifecycle/reporting checks; no utility fixtures or server linkage.
add_executable(tests_reporting_controls EXCLUDE_FROM_ALL reporting_controls.cpp suite_runner.h)
target_link_libraries(tests_reporting_controls cppunit)

target_link_libraries(tests PRIVATE common cppunit)
target_include_directories(tests PRIVATE ..)
include("${CMAKE_SOURCE_DIR}/cmake/ActorBridge.cmake")
eqemu_link_actor_bridge(tests)

install(TARGETS tests RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)

if(MSVC)
	set_target_properties(tests PROPERTIES LINK_FLAGS_RELEASE "/OPT:REF /OPT:ICF")
	target_link_libraries(tests PRIVATE "Ws2_32.lib")
endif()

if(MINGW)
	target_link_libraries(tests PRIVATE "WS2_32")
endif()

if(UNIX)
	target_link_libraries(tests PRIVATE "${CMAKE_DL_LIBS}")
	target_link_libraries(tests PRIVATE "z")
	target_link_libraries(tests PRIVATE "m")
	if(NOT DARWIN)
		target_link_libraries(tests PRIVATE "rt")
	endif()
	target_link_libraries(tests PRIVATE "pthread")
	add_definitions(-fPIC)
endif()

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
set_property(TARGET tests PROPERTY FOLDER executables/tests)

# Policy-only shim builds the maintained module, without linking a game server or fixture DB.
add_executable(tests_actor_lifecycle_policy EXCLUDE_FROM_ALL actor_lifecycle_policy.cpp ../zone/actor_lifecycle.cpp)
target_include_directories(tests_actor_lifecycle_policy BEFORE PRIVATE actor_lifecycle_fakes ..)

add_executable(tests_actor_lifecycle_command_controls EXCLUDE_FROM_ALL actor_lifecycle_command_controls.cpp ../zone/cli/tests/actor_lifecycle_command.cpp)
target_include_directories(tests_actor_lifecycle_command_controls PRIVATE ..)
