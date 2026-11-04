cmake_minimum_required(VERSION 3.20.0)

add_subdirectory(cppunit)

# Standalone entry points for controls also registered in the utility suite.
add_executable(tests_actor_demo_controls EXCLUDE_FROM_ALL actor_demo_controls.cpp)
add_executable(tests_actor_hunt_controls EXCLUDE_FROM_ALL actor_hunt_controls.cpp)

set(tests_sources
	main.cpp
	actor_demo_controls.cpp
	actor_hunt_controls.cpp
)

set(tests_headers
	actor_runtime_test.h
	actor_staging_test.h
	suite_runner.h
	atobool_test.h
	data_verification_test.h
	fixed_memory_test.h
	fixed_memory_variable_test.h
	hextoi_32_64_test.h
	inserver_test_progress_test.h
	ipc_mutex_test.h
	memory_mapped_file_test.h
	string_util_test.h
	skills_util_test.h
	task_state_test.h
)

add_executable(tests ${tests_headers})
target_compile_definitions(tests PRIVATE EQEMU_ACTOR_CONTROLS_IN_UTILITY_SUITE)

if(EQEMU_SANITIZERS)
	# Deliberate faults run separately in the disposable worker, never in the suite.
	add_executable(tests_sanitizer_controls EXCLUDE_FROM_ALL sanitizer_controls.cpp)
endif()

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
if(TARGET actor_bridge)
    list(APPEND tests_sources actor_hunt_bridge_controls.cpp actor_population_bridge_controls.cpp)
    add_executable(tests_actor_hunt_bridge_controls EXCLUDE_FROM_ALL actor_hunt_bridge_controls.cpp)
    eqemu_link_actor_bridge(tests_actor_hunt_bridge_controls)
    add_executable(tests_actor_population_bridge_controls EXCLUDE_FROM_ALL actor_population_bridge_controls.cpp)
    eqemu_link_actor_bridge(tests_actor_population_bridge_controls)
endif()
target_sources(tests PRIVATE ${tests_sources})

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
