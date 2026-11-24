fn main() {
    cxx_build::bridges(["src/lib.rs", "src/population.rs", "src/integration.rs"])
        .std("c++20")
        .compile("actor-bridge");
    println!("cargo:rerun-if-changed=src/lib.rs");
    println!("cargo:rerun-if-changed=src/population.rs");
    println!("cargo:rerun-if-changed=src/integration.rs");
}
