//! Explicit frozen-asset admission. Geometry arithmetic and tree bounds stay in the core.
use crate::{Error, Raycaster, V};
use std::{
    fs::File,
    io::{Read, Write},
    process::{Command, Stdio},
};
type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;
const DEFAULT_BYTES: usize = 100_000_000;
fn bounded(mut f: impl Read, limit: usize) -> Result<Vec<u8>> {
    let mut b = Vec::new();
    // Fixed-size reads cannot grow the allocation past the admitted byte bound.
    let mut chunk = [0; 65536];
    loop {
        let n = f.read(&mut chunk)?;
        if n == 0 {
            return Ok(b);
        }
        if n > limit.saturating_sub(b.len()) {
            return Err(Error("mesh byte limit").into());
        }
        b.try_reserve_exact(n)?;
        b.extend_from_slice(&chunk[..n]);
    }
}
fn hash(b: &[u8]) -> Result<String> {
    // Hash exactly the bytes parsed, avoiding a path re-open race. Fixed executable, no shell.
    let mut p = Command::new("/usr/bin/sha256sum")
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .spawn()?;
    let write = p.stdin.take().unwrap().write_all(b);
    let out = p.wait_with_output()?;
    write?;
    if !out.status.success() {
        return Err(Error("hash tool failed").into());
    }
    Ok(String::from_utf8(out.stdout)?
        .split_whitespace()
        .next()
        .ok_or(Error("hash output"))?
        .to_owned())
}
fn length(nv: usize, ni: usize) -> Option<usize> {
    84usize
        .checked_add(nv.checked_mul(12)?)?
        .checked_add(ni.checked_mul(4)?)
}
fn word(b: &[u8], p: usize) -> usize {
    u32::from_le_bytes(b[p..p + 4].try_into().unwrap()) as usize
}
pub fn load(
    path: &str,
    identity: &str,
    policy: Option<(&str, &str)>,
    read_phase: impl FnOnce(),
) -> Result<Raycaster> {
    let entry = if let Some((name, original)) = policy {
        let fields = include_str!("assets.inc")
            .lines()
            .filter_map(|l| {
                let f: Vec<_> = l.split('|').collect();
                (f.len() == 7 && f[0] == name).then_some(f)
            })
            .next()
            .ok_or(Error("unknown asset policy path"))?;
        if identity != fields[2] {
            return Err(Error("policy input identity").into());
        }
        let input = bounded(File::open(original)?, fields[1].parse()?)?;
        if input.len() != fields[1].parse::<usize>()? || hash(&input)? != fields[2] {
            return Err(Error("policy input hash/length").into());
        }
        Some(fields)
    } else {
        None
    };
    let (nvmax, nimax, limit) = if let Some(f) = &entry {
        (f[3].parse()?, f[4].parse()?, f[5].parse()?)
    } else {
        (4_000_000, 12_000_000, DEFAULT_BYTES)
    };
    let b = bounded(File::open(path)?, limit)?;
    if b.len() < 84 || &b[..8] != b"EQMESH01" || word(&b, 8) != 1 {
        return Err(Error("mesh schema").into());
    }
    let (nv, ni) = (word(&b, 12), word(&b, 16));
    if nv > nvmax || ni > nimax || ni % 3 != 0 || length(nv, ni) != Some(b.len()) {
        return Err(Error("mesh counts/length").into());
    }
    if &b[20..84] != identity.as_bytes() {
        return Err(Error("mesh identity").into());
    }
    if let Some(f) = entry {
        if nv != nvmax || ni != nimax || b.len() != limit || hash(&b)? != f[6] {
            return Err(Error("policy mesh hash/counts/length").into());
        }
    }
    let mut v: Vec<V> = Vec::new();
    v.try_reserve_exact(nv)?;
    for p in (84..84 + nv * 12).step_by(12) {
        v.push(std::array::from_fn(|k| {
            f32::from_bits(word(&b, p + k * 4) as u32)
        }));
    }
    let mut i = Vec::new();
    i.try_reserve_exact(ni)?;
    for p in (84 + nv * 12..b.len()).step_by(4) {
        i.push(word(&b, p) as u32);
    }
    drop(b);
    read_phase();
    Ok(Raycaster::build_admitted(v, i, nvmax, nimax)?)
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn byte_and_arithmetic_bounds() {
        assert_eq!(length(usize::MAX, 3), None);
        assert_eq!(length(0, usize::MAX), None);
        assert_eq!(length(6_161_009, 6_684_999), Some(100_672_188));
        assert!(100_672_188 > DEFAULT_BYTES && 100_672_188 < 100 * 1024 * 1024);
        assert!(bounded(&b"1234"[..], 3).is_err());
        assert_eq!(bounded(&b"123"[..], 3).unwrap(), b"123");
        assert_eq!(
            hash(b"abc").unwrap(),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        );
    }
}
