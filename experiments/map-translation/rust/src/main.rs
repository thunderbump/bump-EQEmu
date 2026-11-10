#![forbid(unsafe_code)]
use sha2::{Digest, Sha256};
use std::{
    fs::{self, OpenOptions},
    io::{BufWriter, Write},
    time::Instant,
};
fn main() {
    if let Err(e) = run() {
        eprintln!("map-poc: {e}");
        std::process::exit(1);
    }
}
fn run() -> Result<(), Box<dyn std::error::Error>> {
    let a: Vec<_> = std::env::args_os().collect();
    if a.len() != 4 {
        return Err("usage: map-poc input.map output.mesh input-sha256".into());
    }
    let start = Instant::now();
    let input = fs::read(&a[1])?;
    let hash = format!("{:x}", Sha256::digest(&input));
    if a[3].to_str() != Some(&hash) {
        return Err("input SHA256 mismatch".into());
    }
    let loaded = start.elapsed();
    let mesh = map_poc::decode_map(&input)?;
    let decoded = start.elapsed();
    let mut out = BufWriter::new(
        OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&a[2])?,
    );
    out.write_all(b"EQMESH01")?;
    for word in [1, mesh.vertices.len() as u32, mesh.indices.len() as u32] {
        out.write_all(&word.to_le_bytes())?;
    }
    out.write_all(hash.as_bytes())?;
    for v in &mesh.vertices {
        for f in v {
            out.write_all(&f.to_bits().to_le_bytes())?;
        }
    }
    for i in &mesh.indices {
        out.write_all(&i.to_le_bytes())?;
    }
    out.flush()?;
    eprintln!("{{\"load_hash_seconds\":{},\"decode_expand_seconds\":{},\"serialize_seconds\":{},\"vertices\":{},\"indices\":{}}}", loaded.as_secs_f64(), (decoded-loaded).as_secs_f64(), (start.elapsed()-decoded).as_secs_f64(), mesh.vertices.len(), mesh.indices.len());
    Ok(())
}
