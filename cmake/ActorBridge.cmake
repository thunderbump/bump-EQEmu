# The pinned archive and its native libraries are Linux x86-64 only. In
# particular, do not discover Cargo or add a startup reference on Windows.
function(eqemu_link_actor_bridge target)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux"
            OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$"
            OR CMAKE_CROSSCOMPILING)
        return()
    endif()

    find_package(Threads REQUIRED)

    if(TARGET actor_bridge)
        get_target_property(actor_headers actor_bridge EQEMU_ACTOR_HEADERS)
        target_link_libraries(${target} PRIVATE actor_bridge Threads::Threads)
        target_include_directories(${target} PRIVATE "${actor_headers}")
        target_compile_definitions(${target} PRIVATE EQEMU_RUST_BOOTSTRAP_BRIDGE)
        add_dependencies(${target} actor_bridge_build)
        return()
    endif()

    # Cargo alone compiles generated CXX code; native callers include its header.
    # Use the guest's Cargo selection and inherit its pinned RUSTC/RUSTDOC,
    # CARGO_HOME and offline dependency environment. Never execute Cargo at configure.
    if(DEFINED ENV{CARGO} AND NOT "$ENV{CARGO}" STREQUAL "")
        set(actor_cargo "$ENV{CARGO}")
    else()
        find_program(actor_cargo NAMES cargo REQUIRED)
    endif()

    if(DEFINED ENV{CARGO_TARGET_DIR} AND NOT "$ENV{CARGO_TARGET_DIR}" STREQUAL "")
        set(actor_target_dir "$ENV{CARGO_TARGET_DIR}")
    else()
        # A sibling of the source tree also works for -B build and nested presets.
        # Keep independent CMake build trees from sharing Cargo's output/clean state.
        file(REAL_PATH "${CMAKE_BINARY_DIR}" actor_binary_dir)
        string(SHA256 actor_build_id "${actor_binary_dir}")
        get_filename_component(actor_source_parent "${CMAKE_SOURCE_DIR}" DIRECTORY)
        set(actor_target_dir "${actor_source_parent}/.eqemu-rust-target/${actor_build_id}")
    endif()
    file(REAL_PATH "${actor_target_dir}" actor_target_dir BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    file(REAL_PATH "${CMAKE_SOURCE_DIR}" actor_source_dir)
    string(FIND "${actor_target_dir}/" "${actor_source_dir}/" actor_target_in_source)
    if(actor_target_in_source EQUAL 0)
        message(FATAL_ERROR "actor-bridge requires a Cargo target directory outside the source tree")
    endif()

    set(actor_archive "${actor_target_dir}/x86_64-unknown-linux-gnu/release/libactor_bridge.a")
    # Always let Cargo check its own inputs; its incremental build also handles
    # changes to build.rs, generated CXX code and the locked dependency graph.
    add_custom_target(actor_bridge_build
        COMMAND "${CMAKE_COMMAND}" -E env
            "CARGO_TARGET_DIR=${actor_target_dir}"
            "CARGO_NET_OFFLINE=true"
            "${actor_cargo}" build
            --manifest-path "${CMAKE_SOURCE_DIR}/rust/actors/Cargo.toml"
            --package actor-bridge --release --frozen --offline
            --target x86_64-unknown-linux-gnu
        BYPRODUCTS "${actor_archive}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Building the Rust actor bridge (offline, frozen)"
        VERBATIM
    )
    add_library(actor_bridge STATIC IMPORTED GLOBAL)
    set_target_properties(actor_bridge PROPERTIES IMPORTED_LOCATION "${actor_archive}")
    add_dependencies(actor_bridge actor_bridge_build)

    # Native libraries required by the pinned Linux Rust staticlib.
    set_property(TARGET actor_bridge PROPERTY INTERFACE_LINK_LIBRARIES
        "${CMAKE_DL_LIBS};m;rt;util")
    # CXX exposes headers beside Cargo's build profiles; include both native
    # and explicit-target layouts. Generated code remains compiled only by Cargo.
    set(actor_headers "${actor_target_dir}/cxxbridge;${actor_target_dir}/x86_64-unknown-linux-gnu/cxxbridge")
    set_target_properties(actor_bridge PROPERTIES EQEMU_ACTOR_HEADERS "${actor_headers}")
    target_include_directories(${target} PRIVATE "${actor_headers}")
    add_dependencies(${target} actor_bridge_build)
    target_link_libraries(${target} PRIVATE actor_bridge Threads::Threads)
    target_compile_definitions(${target} PRIVATE EQEMU_RUST_BOOTSTRAP_BRIDGE)
endfunction()
