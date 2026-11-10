#![forbid(unsafe_code)]
//! Bounded V2 individual-placement experiment. No server or actor dependencies.
use flate2::{Decompress, FlushDecompress, Status};
use std::collections::BTreeMap;

#[derive(Debug, PartialEq, Eq)]
pub struct MapError(pub &'static str);
impl std::fmt::Display for MapError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(self.0)
    }
}
impl std::error::Error for MapError {}
type Result<T> = std::result::Result<T, MapError>;
#[derive(Debug)]
pub struct Mesh {
    pub vertices: Vec<[f32; 3]>,
    pub indices: Vec<u32>,
}
const MAX_BYTES: usize = 128 * 1024 * 1024;
const MAX_VERTICES: usize = 4_000_000;
const MAX_INDICES: usize = 12_000_000;
struct Cursor<'a>(&'a [u8]);
impl<'a> Cursor<'a> {
    fn take(&mut self, n: usize) -> Result<&'a [u8]> {
        let value = self.0.get(..n).ok_or(MapError("truncated data"))?;
        self.0 = &self.0[n..];
        Ok(value)
    }
    fn word(&mut self) -> Result<u32> {
        Ok(u32::from_le_bytes(self.take(4)?.try_into().unwrap()))
    }
    fn float(&mut self) -> Result<f32> {
        Ok(f32::from_bits(self.word()?))
    }
    fn triple(&mut self) -> Result<[f32; 3]> {
        Ok([self.float()?, self.float()?, self.float()?])
    }
    fn name(&mut self) -> Result<Vec<u8>> {
        let n = self
            .0
            .iter()
            .position(|&b| b == 0)
            .ok_or(MapError("unterminated name"))?;
        Ok(self.take(n + 1)?[..n].to_vec())
    }
    fn skip_records(&mut self, count: usize, size: usize) -> Result<()> {
        self.take(count.checked_mul(size).ok_or(MapError("count overflow"))?)?;
        Ok(())
    }
}
fn reserve<T>(v: &mut Vec<T>, n: usize, limit: usize) -> Result<()> {
    if v.len().checked_add(n).ok_or(MapError("count overflow"))? > limit {
        return Err(MapError("mesh limit"));
    }
    v.try_reserve(n).map_err(|_| MapError("allocation failed"))
}
/// Decode one complete little-endian zlib V2 file. Resource limits are deliberate
/// PoC limits, not claims about every map. Reject unsupported geometry explicitly.
pub fn decode_map(input: &[u8]) -> Result<Mesh> {
    let mut file = Cursor(input);
    if file.word()? != 0x02000000 {
        return Err(MapError("unsupported map version"));
    }
    let packed = file.word()? as usize;
    let size = file.word()? as usize;
    if size > MAX_BYTES || packed > MAX_BYTES {
        return Err(MapError("payload limit"));
    }
    let compressed = file.take(packed)?;
    if !file.0.is_empty() {
        return Err(MapError("trailing file bytes"));
    }
    let mut data = Vec::new();
    data.try_reserve_exact(size)
        .map_err(|_| MapError("allocation failed"))?;
    data.resize(size, 0);
    let mut z = Decompress::new(true);
    let status = z
        .decompress(compressed, &mut data, FlushDecompress::Finish)
        .map_err(|_| MapError("invalid zlib"))?;
    if status != Status::StreamEnd || z.total_in() != packed as u64 || z.total_out() != size as u64
    {
        return Err(MapError("zlib length mismatch"));
    }
    expand(&data)
}
fn expand(data: &[u8]) -> Result<Mesh> {
    let mut c = Cursor(data);
    let nv = c.word()? as usize;
    let ni = c.word()? as usize;
    let nnv = c.word()? as usize;
    let nni = c.word()? as usize;
    let nm = c.word()?;
    let np = c.word()?;
    let groups = c.word()?;
    let tiles = c.word()?;
    let _quads = c.word()?;
    let _units = c.float()?;
    if groups != 0 {
        return Err(MapError("unsupported placement groups"));
    }
    if tiles != 0 {
        return Err(MapError("unsupported terrain"));
    }
    if ni % 3 != 0 {
        return Err(MapError("nontriangle base indices"));
    }
    let mut mesh = Mesh {
        vertices: Vec::new(),
        indices: Vec::new(),
    };
    // Check available bytes before reserving based on untrusted counts.
    if nv > c.0.len() / 12 {
        return Err(MapError("truncated vertices"));
    }
    reserve(&mut mesh.vertices, nv, MAX_VERTICES)?;
    for _ in 0..nv {
        mesh.vertices.push(c.triple()?);
    }
    if ni > c.0.len() / 4 {
        return Err(MapError("truncated indices"));
    }
    reserve(&mut mesh.indices, ni, MAX_INDICES)?;
    for _ in 0..ni {
        let i = c.word()?;
        if i as usize >= nv {
            return Err(MapError("base index out of bounds"));
        }
        mesh.indices.push(i);
    }
    c.skip_records(nnv, 12)?;
    c.skip_records(nni, 4)?;
    let mut models = BTreeMap::new();
    for _ in 0..nm {
        let name = c.name()?;
        let nv = c.word()? as usize;
        let nt = c.word()? as usize;
        if nv > c.0.len() / 12 {
            return Err(MapError("truncated model vertices"));
        }
        let mut vertices = Vec::new();
        reserve(&mut vertices, nv, MAX_VERTICES)?;
        for _ in 0..nv {
            vertices.push(c.triple()?);
        }
        if nt > c.0.len() / 13 {
            return Err(MapError("truncated polygons"));
        }
        let mut polys = Vec::new();
        reserve(&mut polys, nt, MAX_INDICES / 3)?;
        for _ in 0..nt {
            let ids = [c.word()?, c.word()?, c.word()?];
            let visible = c.take(1)?[0];
            if ids.iter().any(|&i| i as usize >= nv) {
                return Err(MapError("model index out of bounds"));
            }
            polys.push((ids, visible));
        }
        models.insert(name, (vertices, polys)); // Raw bytes; later definitions replace earlier ones.
    }
    for _ in 0..np {
        let name = c.name()?;
        let translation = c.triple()?;
        let rotation = c.triple()?;
        let scale = c.triple()?;
        if let Some((vertices, polys)) = models.get(&name) {
            for &(ids, visible) in polys {
                if visible == 0 {
                    continue;
                }
                reserve(&mut mesh.vertices, 3, MAX_VERTICES)?;
                reserve(&mut mesh.indices, 3, MAX_INDICES)?;
                for id in ids {
                    let v = transform(vertices[id as usize], rotation, scale, translation);
                    mesh.indices.push(mesh.vertices.len() as u32);
                    mesh.vertices.push(v);
                }
            }
        }
    }
    if !c.0.is_empty() {
        return Err(MapError("trailing payload bytes"));
    }
    Ok(mesh)
}
// Match production's f32 intermediate assignments and X, Y, Z rotation sequence.
fn transform(mut v: [f32; 3], r: [f32; 3], s: [f32; 3], t: [f32; 3]) -> [f32; 3] {
    let y = r[0].cos() * v[1] - r[0].sin() * v[2];
    let z = r[0].sin() * v[1] + r[0].cos() * v[2];
    v[1] = y;
    v[2] = z;
    let x = r[1].cos() * v[0] + r[1].sin() * v[2];
    let z = -(r[1].sin() * v[0]) + r[1].cos() * v[2];
    v[0] = x;
    v[2] = z;
    let x = r[2].cos() * v[0] - r[2].sin() * v[1];
    let y = r[2].sin() * v[0] + r[2].cos() * v[1];
    v[0] = x;
    v[1] = y;
    for i in 0..3 {
        v[i] = v[i] * s[i];
        v[i] = v[i] + t[i];
    }
    [v[1], v[0], v[2]]
}
