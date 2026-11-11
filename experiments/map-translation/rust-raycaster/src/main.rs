#![forbid(unsafe_code)]
use rust_raycaster_poc::{Error, Raycaster, V};
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
fn load(path: &str, identity: &[u8]) -> Result<(Vec<V>, Vec<u32>), Box<dyn std::error::Error>> {
    let mut f = fs::File::open(path)?;
    if f.metadata()?.len() > 100_000_000 {
        return Err(Error("mesh byte limit").into());
    }
    let mut b = Vec::new();
    b.try_reserve_exact(f.metadata()?.len() as usize)?;
    f.read_to_end(&mut b)?;
    if b.len() < 84 || &b[..8] != b"EQMESH01" || word(&b, 8) != 1 {
        return Err(Error("mesh schema").into());
    }
    let (nv, ni) = (word(&b, 12) as usize, word(&b, 16) as usize);
    if nv > 4_000_000 || ni > 12_000_000 || ni % 3 != 0 || b.len() != 84 + nv * 12 + ni * 4 {
        return Err(Error("mesh counts/length").into());
    }
    if &b[20..84] != identity {
        return Err(Error("mesh identity").into());
    }
    let mut v = Vec::new();
    v.try_reserve_exact(nv)?;
    for p in (84..84 + nv * 12).step_by(12) {
        v.push([
            f32::from_bits(word(&b, p)),
            f32::from_bits(word(&b, p + 4)),
            f32::from_bits(word(&b, p + 8)),
        ]);
    }
    let mut i = Vec::new();
    i.try_reserve_exact(ni)?;
    for p in (84 + nv * 12..b.len()).step_by(4) {
        i.push(word(&b, p));
    }
    Ok((v, i))
}
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let a: Vec<_> = std::env::args().collect();
    if a.len() != 5 {
        return Err(Error("usage: mesh identity queries.bin output.bin").into());
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
    let (v, i) = load(&a[1], a[2].as_bytes())?;
    phase("read", t);
    let t = Instant::now();
    let mut engine = Raycaster::build(v, i)?;
    phase("build", t);
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
    out.flush()?;
    phase("query_and_serialize", t);
    eprintln!("{{\"query_seconds\":{query_seconds},\"serialize_seconds\":{serialize_seconds},\"queries\":{count}}}");
    drop(engine);
    phase("release", t);
    Ok(())
}
