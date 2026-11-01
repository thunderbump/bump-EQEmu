//! Single-owner bounded checkpoint. A dirty checkpoint is evidence requiring
//! operator recovery; it is never interpreted as permission to spawn bodies.
use actor_core::population::{COUNT, Desired, HOME, PROFILE, Point, Saved};
use serde::{Deserialize, Serialize};
use std::os::unix::fs::{OpenOptionsExt, PermissionsExt};
use std::{
    fs::{self, File, OpenOptions},
    io::{Read, Write},
    path::{Path, PathBuf},
    time::Instant,
};

pub const LIMIT: u64 = 65_536;
#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct Record {
    id: String,
    state: String,
    home: [f64; 3],
    position: [f64; 3],
    hp: i64,
    rest_ms: u64,
    completed: u64,
    yielded: u64,
    failed: u64,
}
#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct Disk {
    schema: u32,
    zone: u32,
    profile: String,
    revision: u64,
    clean: bool,
    actors: Vec<Record>,
}
pub struct Loaded {
    pub saved: [Saved; COUNT],
    pub revision: u64,
    pub clean: bool,
}
pub struct Store {
    pub directory: PathBuf,
    _lock: File,
    pub sync_us: u64,
}
impl Drop for Store {
    fn drop(&mut self) {
        // Close-on-exec still permits a brief inherited descriptor during
        // subprocess startup. Dropping this owner must release its lock now.
        let _ = self._lock.unlock();
    }
}
impl Store {
    pub fn open(directory: &str) -> Result<Self, String> {
        let path = Path::new(directory);
        if !path.is_absolute() || directory.len() > 512 {
            return Err("invalid_directory".into());
        }
        let metadata = fs::metadata(path).map_err(|e| format!("directory: {e}"))?;
        if !metadata.is_dir() || metadata.permissions().mode() & 0o077 != 0 {
            return Err("private_directory_required".into());
        }
        let lock = OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .truncate(false)
            .mode(0o600)
            .open(path.join("owner.lock"))
            .map_err(|e| format!("owner_lock: {e}"))?;
        lock.try_lock()
            .map_err(|e| format!("owner_lock_refused: {e}"))?;
        Ok(Self {
            directory: path.into(),
            _lock: lock,
            sync_us: 0,
        })
    }
    pub fn load(&self) -> Result<Loaded, String> {
        let path = self.directory.join("checkpoint.json");
        let file = match File::open(path) {
            Ok(f) => f,
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => {
                return Ok(Loaded {
                    saved: std::array::from_fn(Saved::initial),
                    revision: 0,
                    clean: true,
                });
            }
            Err(e) => return Err(format!("checkpoint_read: {e}")),
        };
        let mut bytes = Vec::new();
        file.take(LIMIT + 1)
            .read_to_end(&mut bytes)
            .map_err(|e| format!("checkpoint_read: {e}"))?;
        if bytes.len() as u64 > LIMIT {
            return Err("checkpoint_too_large".into());
        }
        decode(&bytes)
    }
    pub fn write(
        &mut self,
        saved: &[Saved; COUNT],
        revision: u64,
        clean: bool,
    ) -> Result<(), String> {
        if !saved.iter().all(Saved::valid) {
            return Err("invalid_native_checkpoint_facts".into());
        }
        let disk = Disk {
            schema: 1,
            zone: 2,
            profile: PROFILE.into(),
            revision,
            clean,
            actors: saved
                .iter()
                .enumerate()
                .map(|(i, s)| Record {
                    id: format!("hunter-{}", i + 1),
                    state: s.desired.name().into(),
                    home: [HOME.x, HOME.y, HOME.z],
                    position: [s.point.x, s.point.y, s.point.z],
                    hp: s.hp,
                    rest_ms: s.rest_ms,
                    completed: s.completed,
                    yielded: s.yielded,
                    failed: s.failed,
                })
                .collect(),
        };
        let bytes = serde_json::to_vec(&disk).map_err(|e| e.to_string())?;
        if bytes.len() as u64 > LIMIT {
            return Err("checkpoint_too_large".into());
        }
        let began = Instant::now();
        atomic(&self.directory, "checkpoint.json", &bytes)?;
        self.sync_us = began.elapsed().as_micros().min(u128::from(u64::MAX)) as u64;
        Ok(())
    }
}
fn decode(bytes: &[u8]) -> Result<Loaded, String> {
    let disk: Disk =
        serde_json::from_slice(bytes).map_err(|e| format!("checkpoint_schema: {e}"))?;
    if disk.revision == 0
        || disk.schema != 1
        || disk.zone != 2
        || disk.profile != PROFILE
        || disk.actors.len() != COUNT
    {
        return Err("checkpoint_profile_or_schema".into());
    }
    let mut saved = std::array::from_fn(Saved::initial);
    for (i, r) in disk.actors.into_iter().enumerate() {
        if r.id != format!("hunter-{}", i + 1) || r.home != [HOME.x, HOME.y, HOME.z] {
            return Err("checkpoint_actor_identity_or_home".into());
        }
        let desired = match r.state.as_str() {
            "enabled" => Desired::Enabled,
            "paused" => Desired::Paused,
            "dead" => Desired::Dead,
            _ => return Err("checkpoint_actor_state".into()),
        };
        saved[i] = Saved {
            desired,
            point: Point {
                x: r.position[0],
                y: r.position[1],
                z: r.position[2],
            },
            hp: r.hp,
            rest_ms: r.rest_ms,
            completed: r.completed,
            yielded: r.yielded,
            failed: r.failed,
        };
        if !saved[i].valid() {
            return Err("checkpoint_actor_facts".into());
        }
    }
    Ok(Loaded {
        saved,
        revision: disk.revision,
        clean: disk.clean,
    })
}
pub fn atomic(directory: &Path, name: &str, bytes: &[u8]) -> Result<(), String> {
    let temporary = directory.join(format!("{name}.tmp"));
    let mut out = OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .mode(0o600)
        .open(&temporary)
        .map_err(|e| format!("checkpoint_write: {e}"))?;
    out.write_all(bytes)
        .and_then(|_| out.sync_all())
        .map_err(|e| format!("checkpoint_sync: {e}"))?;
    fs::rename(&temporary, directory.join(name)).map_err(|e| format!("checkpoint_rename: {e}"))?;
    File::open(directory)
        .and_then(|d| d.sync_all())
        .map_err(|e| format!("directory_sync: {e}"))
}

#[cfg(test)]
mod tests {
    use super::*;
    pub fn directory() -> PathBuf {
        static NEXT: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
        let p = std::env::temp_dir().join(format!(
            "eqemu-population-test-{}-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos(),
            NEXT.fetch_add(1, std::sync::atomic::Ordering::Relaxed)
        ));
        fs::create_dir(&p).unwrap();
        fs::set_permissions(&p, fs::Permissions::from_mode(0o700)).unwrap();
        p
    }
    #[test]
    fn roundtrip_wounded_rest_and_single_owner() {
        let p = directory();
        let mut store = Store::open(p.to_str().unwrap()).unwrap();
        assert!(Store::open(p.to_str().unwrap()).is_err());
        let mut s = std::array::from_fn(Saved::initial);
        s[0].desired = Desired::Enabled;
        s[0].hp = 47;
        s[0].rest_ms = 6172;
        s[1].desired = Desired::Dead;
        s[1].hp = 0;
        for revision in 1..4 {
            store.write(&s, revision, true).unwrap();
            let l = store.load().unwrap();
            assert_eq!(l.saved[0].hp, 47);
            assert_eq!(l.saved[0].rest_ms, 6172);
            assert_eq!(l.saved[1].desired, Desired::Dead);
            assert_eq!(l.revision, revision);
        }
        // A concurrent subprocess may briefly inherit this descriptor before exec.
        let inherited = store._lock.try_clone().unwrap();
        drop(store);
        assert!(Store::open(p.to_str().unwrap()).is_ok());
        drop(inherited);
        fs::remove_dir_all(p).unwrap();
    }
    #[test]
    fn corruption_schema_ids_and_io_failures_preserve_evidence() {
        let p = directory();
        let mut store = Store::open(p.to_str().unwrap()).unwrap();
        let s = std::array::from_fn(Saved::initial);
        store.write(&s, 1, true).unwrap();
        let valid = fs::read(p.join("checkpoint.json")).unwrap();
        for changed in [
            b"not json".to_vec(),
            String::from_utf8(valid.clone())
                .unwrap()
                .replace("\"schema\":1", "\"schema\":2")
                .into_bytes(),
            String::from_utf8(valid.clone())
                .unwrap()
                .replace("hunter-2", "hunter-1")
                .into_bytes(),
        ] {
            fs::write(p.join("checkpoint.json"), &changed).unwrap();
            assert!(store.load().is_err());
            assert_eq!(fs::read(p.join("checkpoint.json")).unwrap(), changed);
        }
        fs::write(p.join("checkpoint.json"), &valid).unwrap();
        fs::create_dir(p.join("checkpoint.json.tmp")).unwrap();
        assert!(store.write(&s, 2, false).is_err());
        assert_eq!(fs::read(p.join("checkpoint.json")).unwrap(), valid);
        fs::remove_dir(p.join("checkpoint.json.tmp")).unwrap();
        fs::remove_file(p.join("checkpoint.json")).unwrap();
        fs::create_dir(p.join("checkpoint.json")).unwrap();
        assert!(store.write(&s, 2, false).unwrap_err().contains("rename"));
        fs::remove_dir_all(p).unwrap();
    }
}
