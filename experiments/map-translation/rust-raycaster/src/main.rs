#![forbid(unsafe_code)]
use rust_raycaster_poc::Error;
use std::{
    fs::{self, OpenOptions},
    io::{BufWriter, Read, Write},
    time::Instant,
};
fn word(b: &[u8], p: usize) -> u32 {
    u32::from_le_bytes(b[p..p + 4].try_into().unwrap())
}
fn phase(s: &str, t: Instant) {
    eprintln!(
        "PHASE {s} seconds={} {}",
        t.elapsed().as_secs_f64(),
        fs::read_to_string("/proc/self/status")
            .unwrap_or_default()
            .lines()
            .filter(|s| s.starts_with("VmRSS:") || s.starts_with("VmHWM:"))
            .collect::<Vec<_>>()
            .join(" ")
    );
}
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let a: Vec<_> = std::env::args().collect();
    if a.len() != 5 && a.len() != 8 {
        return Err(Error("usage: mesh identity queries.bin output.bin [--known-asset base/name.map original.map]").into());
    }
    if a[2].len() != 64
        || !a[2]
            .bytes()
            .all(|b| b.is_ascii_hexdigit() && !b.is_ascii_uppercase())
    {
        return Err(Error("identity syntax").into());
    }
    let t = Instant::now();
    phase("start", t);
    let policy = if a.len() == 8 {
        if a[5] != "--known-asset" {
            return Err(Error("policy syntax").into());
        }
        Some((a[6].as_str(), a[7].as_str()))
    } else {
        None
    };
    let mut engine = rust_raycaster_poc::admission::load(&a[1], &a[2], policy, || {
        phase("read", t);
        build_start.set(Instant::now());
    })?;
    phase("build", build_start.get());
    let mut input = fs::File::open(&a[3])?;
    let mut header = [0u8; 76];
    input.read_exact(&mut header)?;
    if &header[..8] != b"EQRAYS01" || &header[8..72] != a[2].as_bytes() {
        return Err(Error("replay identity/schema").into());
    }
    let count = word(&header, 72);
    if count == 0 || count > 100_000 {
        return Err(Error("replay count").into());
    }
    if input.metadata()?.len() != 76 + u64::from(count) * 28 {
        return Err(Error("replay length").into());
    }
    let mut out = BufWriter::new(
        OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&a[4])?,
    );
    let mut query_seconds = 0.;
    let mut serialize_seconds = 0.;
    let t = Instant::now();
    for ordinal in 0..count {
        let mut row = [0u8; 28];
        input.read_exact(&mut row)?;
        if word(&row, 0) != ordinal {
            return Err(Error("replay order").into());
        }
        let x: [f32; 6] = std::array::from_fn(|k| f32::from_bits(word(&row, 4 + k * 4)));
        let q = Instant::now();
        let hit = engine.segment(x[..3].try_into()?, x[3..].try_into()?)?;
        query_seconds += q.elapsed().as_secs_f64();
        let q = Instant::now();
        out.write_all(&[u8::from(hit.is_some())])?;
        if let Some(h) = hit {
            for f in h.position.into_iter().chain(h.normal).chain([h.distance]) {
                if !f.is_finite() {
                    return Err(Error("nonfinite hit").into());
                }
                out.write_all(&f.to_le_bytes())?;
            }
            out.write_all(&(h.triangle as u32).to_le_bytes())?;
        }
        serialize_seconds += q.elapsed().as_secs_f64();
    }
    if input.read(&mut [0u8; 1])? != 0 {
        return Err(Error("replay EOF").into());
    }
    out.flush()?;
    phase("query_and_serialize", t);
    eprintln!("{{\"query_seconds\":{query_seconds},\"serialize_seconds\":{serialize_seconds},\"queries\":{count}}}");
    drop(engine);
    phase("release", t);
    Ok(())
}
