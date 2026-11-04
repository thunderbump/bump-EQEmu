//! Per-actor ownership. A lock covers native custody and every durable write;
//! distinct actors never rewrite one shared state file. Migration publishes all
//! actor records through one directory rename and retains the old checkpoint.
use crate::checkpoint::{self, Loaded, Store as LegacyStore};
use actor_core::population::{COUNT, PROFILE, Saved};
use serde::{Deserialize, Serialize};
use std::fs::{self, File, OpenOptions};
use std::io::{Read, Write};
use std::os::unix::fs::{OpenOptionsExt, PermissionsExt};
use std::path::{Path, PathBuf};
use std::time::Instant;

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct Record {
    schema: u32,
    id: usize,
    profile: String,
    revision: u64,
    clean: bool,
    saved: Saved,
}
struct Owned {
    file: File,
    record: Record,
}
impl Drop for Owned {
    fn drop(&mut self) {
        let _ = self.file.unlock();
    }
}
pub struct Store {
    pub directory: PathBuf,
    pub zone: u32,
    pub sync_us: u64,
    records: PathBuf,
    owned: [Option<Owned>; COUNT],
}
fn record_name(index: usize) -> String {
    format!("actor-{}.json", index + 1)
}
fn read(directory: &Path, index: usize) -> Result<Record, String> {
    let mut bytes = Vec::new();
    File::open(directory.join(record_name(index)))
        .map_err(|e| format!("actor_read: {e}"))?
        .take(checkpoint::LIMIT + 1)
        .read_to_end(&mut bytes)
        .map_err(|e| format!("actor_read: {e}"))?;
    if bytes.len() as u64 > checkpoint::LIMIT {
        return Err("actor_record_too_large".into());
    }
    let record: Record =
        serde_json::from_slice(&bytes).map_err(|e| format!("actor_schema: {e}"))?;
    if record.schema != 3
        || record.id != index
        || record.profile != PROFILE
        || record.revision == 0
        || !record.saved.valid()
    {
        return Err("actor_identity_or_state".into());
    }
    Ok(record)
}
fn publish(directory: &Path, index: usize, record: &Record) -> Result<(), String> {
    let bytes = serde_json::to_vec(record).map_err(|e| format!("actor_encode: {e}"))?;
    if bytes.len() as u64 > checkpoint::LIMIT {
        return Err("actor_record_too_large".into());
    }
    checkpoint::atomic(directory, &record_name(index), &bytes)
}
fn migrate(directory: &str) -> Result<PathBuf, String> {
    // The legacy owner lock serializes migration and excludes a running old
    // controller. Existing v3 owners use independent actor locks afterward.
    let records = Path::new(directory).join("actors-v3");
    if records.exists() {
        let metadata = fs::metadata(directory).map_err(|e| format!("directory: {e}"))?;
        if !Path::new(directory).is_absolute()
            || directory.len() > 512
            || !metadata.is_dir()
            || metadata.permissions().mode() & 0o077 != 0
        {
            return Err("private_directory_required".into());
        }
        File::open(directory)
            .and_then(|file| file.sync_all())
            .map_err(|error| format!("migration_sync: {error}"))?;
        return Ok(records);
    }
    let mut old = LegacyStore::open(directory)?;
    let loaded = old.load()?;
    if !loaded.clean {
        return Err("legacy_recovery_required".into());
    }
    let legacy_saved = loaded.saved.clone();
    let temporary = old
        .directory
        .join(format!("actors-v3-migration-{}", std::process::id()));
    fs::create_dir(&temporary).map_err(|e| format!("migration_directory: {e}"))?;
    fs::set_permissions(&temporary, fs::Permissions::from_mode(0o700))
        .map_err(|e| format!("migration_permissions: {e}"))?;
    for (i, saved) in loaded.saved.into_iter().enumerate() {
        publish(
            &temporary,
            i,
            &Record {
                schema: 3,
                id: i,
                profile: PROFILE.into(),
                revision: loaded.revision.max(1),
                clean: true,
                saved,
            },
        )?;
    }
    File::open(&temporary)
        .and_then(|f| f.sync_all())
        .map_err(|e| format!("migration_sync: {e}"))?;
    // Preserve the original bytes, then inhibit downgrade spawning before
    // publishing v3. A failed migration never leaves two clean authorities.
    match fs::read(old.directory.join("checkpoint.json")) {
        Ok(bytes) => {
            let mut backup = OpenOptions::new()
                .write(true)
                .create_new(true)
                .mode(0o600)
                .open(old.directory.join("checkpoint.pre-v3.json"))
                .map_err(|e| format!("migration_backup: {e}"))?;
            backup
                .write_all(&bytes)
                .and_then(|_| backup.sync_all())
                .map_err(|e| format!("migration_backup: {e}"))?;
        }
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => {}
        Err(error) => return Err(format!("migration_backup: {error}")),
    }
    let revision = loaded.revision.checked_add(1).ok_or("revision_exhausted")?;
    old.write(&legacy_saved, revision, false)?;
    fs::rename(&temporary, &records).map_err(|e| format!("migration_publish: {e}"))?;
    File::open(&old.directory)
        .and_then(|f| f.sync_all())
        .map_err(|e| format!("migration_sync: {e}"))?;
    Ok(records)
}
impl Store {
    pub fn open(directory: &str, zone: u32) -> Result<Self, String> {
        if zone == 0 {
            return Err("invalid_actor_zone".into());
        }
        Ok(Self {
            directory: directory.into(),
            zone,
            sync_us: 0,
            records: migrate(directory)?,
            owned: std::array::from_fn(|_| None),
        })
    }
    pub fn mask(&self) -> u8 {
        self.owned.iter().enumerate().fold(0, |mask, (i, owned)| {
            mask | if owned.is_some() { 1 << i } else { 0 }
        })
    }
    #[cfg(test)]
    pub fn actor_path(&self, index: usize) -> PathBuf {
        self.records.join(record_name(index))
    }
    #[cfg(test)]
    pub fn temporary_path(&self, index: usize) -> PathBuf {
        self.records.join(format!("{}.tmp", record_name(index)))
    }
    pub fn refresh(&mut self) -> Result<Vec<(usize, Saved)>, String> {
        let mut adopted = Vec::new();
        let mut contended = false;
        for i in 0..COUNT {
            if self.owned[i].is_some() {
                continue;
            }
            if read(&self.records, i)?.saved.zone != self.zone {
                continue;
            }
            let file = OpenOptions::new()
                .read(true)
                .write(true)
                .create(true)
                .truncate(false)
                .mode(0o600)
                .open(self.records.join(format!("actor-{}.lock", i + 1)))
                .map_err(|e| format!("actor_lock: {e}"))?;
            if file.try_lock().is_err() {
                contended = true;
                continue;
            }
            let record = read(&self.records, i)?;
            if record.saved.zone != self.zone {
                let _ = file.unlock();
                continue;
            }
            self.owned[i] = Some(Owned { file, record });
            let owned = self.owned[i].as_mut().unwrap();
            if !owned.record.clean {
                return Err(format!("actor_{}_recovery_required", i + 1));
            }
            owned.record.saved.owner_epoch = owned
                .record
                .saved
                .owner_epoch
                .checked_add(1)
                .ok_or("owner_epoch_exhausted")?;
            owned.record.revision = owned
                .record
                .revision
                .checked_add(1)
                .ok_or("revision_exhausted")?;
            // No body exists yet. A clean acquisition record remains restartable;
            // every Spawn still requires a separate dirty durable publication.
            publish(&self.records, i, &owned.record)?;
            adopted.push((i, owned.record.saved.clone()));
        }
        if adopted.is_empty() && self.mask() == 0 && contended {
            return Err("actor_owner_lock_refused".into());
        }
        Ok(adopted)
    }
    pub fn load(&mut self) -> Result<Loaded, String> {
        self.refresh()?;
        let mut saved = std::array::from_fn(Saved::initial);
        let mut revision = 0;
        let mut clean = true;
        for (i, state) in saved.iter_mut().enumerate() {
            let record = read(&self.records, i)?;
            revision = revision.max(record.revision);
            if self.owned[i].is_some() {
                clean &= record.clean;
            }
            *state = record.saved;
        }
        Ok(Loaded {
            saved,
            revision,
            clean,
        })
    }
    pub fn pending_boots(&self) -> Result<Vec<(usize, Saved)>, String> {
        let mut pending = Vec::new();
        for i in 0..COUNT {
            if self.owned[i].is_some() {
                continue;
            }
            let record = read(&self.records, i)?;
            if record.clean
                && record.saved.previous_zone == self.zone
                && record.saved.zone != self.zone
                && record.saved.desired == actor_core::population::Desired::Enabled
            {
                pending.push((i, record.saved));
            }
        }
        Ok(pending)
    }
    pub fn write(
        &mut self,
        saved: &[Saved; COUNT],
        revision: u64,
        clean: bool,
        drained: [bool; COUNT],
    ) -> Result<u64, String> {
        let began = Instant::now();
        let mut published_revision = revision;
        for i in 0..COUNT {
            let Some(owned) = self.owned[i].as_mut() else {
                continue;
            };
            if !saved[i].valid() || saved[i].owner_epoch != owned.record.saved.owner_epoch {
                return Err("actor_write_identity_or_state".into());
            }
            let departing = saved[i].zone != self.zone;
            if departing && !drained[i] {
                return Err("actor_departure_before_native_drain".into());
            }
            let next = owned
                .record
                .revision
                .checked_add(1)
                .ok_or("revision_exhausted")?
                .max(revision);
            let record = Record {
                schema: 3,
                id: i,
                profile: PROFILE.into(),
                revision: next,
                clean: departing || clean,
                saved: saved[i].clone(),
            };
            publish(&self.records, i, &record)?;
            published_revision = published_revision.max(next);
            owned.record = record;
            if departing {
                self.owned[i] = None;
            } // durable destination, then release
        }
        self.sync_us = began.elapsed().as_micros().min(u128::from(u64::MAX)) as u64;
        Ok(published_revision)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use actor_core::inventory::Item;
    use std::sync::{Arc, Barrier};
    fn directory() -> PathBuf {
        static NEXT: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
        let directory = std::env::temp_dir().join(format!(
            "eqemu-custody-{}-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos(),
            NEXT.fetch_add(1, std::sync::atomic::Ordering::Relaxed)
        ));
        fs::create_dir(&directory).unwrap();
        fs::set_permissions(&directory, fs::Permissions::from_mode(0o700)).unwrap();
        directory
    }
    fn progressed(saved: &mut Saved) {
        saved.level = 2;
        saved.experience = 12345;
        saved.reputation.insert(203, -10);
        saved.inventory = Some(vec![
            Item {
                parent: -1,
                kind: 0,
                slot: 23,
                id: 17005,
                serial: 40,
                ..Item::default()
            },
            Item {
                parent: 0,
                kind: 1,
                slot: 2,
                id: 1001,
                serial: 41,
                charges: -1,
                custom: "retained^bag^".into(),
                ..Item::default()
            },
        ]);
    }
    #[test]
    fn transfer_requires_drain_and_acquires_once_with_complete_state() {
        let directory = directory();
        let mut source = Store::open(directory.to_str().unwrap(), 2).unwrap();
        let mut saved = source.load().unwrap().saved;
        progressed(&mut saved[0]);
        source.write(&saved, 10, false, [false; COUNT]).unwrap();
        let epoch = saved[0].owner_epoch;
        let mut destination = Store::open(directory.to_str().unwrap(), 3).unwrap();
        assert_eq!(destination.load().unwrap().saved[0].zone, 2);
        assert_eq!(destination.mask(), 0);
        saved[0].zone = 3;
        saved[0].previous_zone = 2;
        assert!(source.write(&saved, 11, false, [false; COUNT]).is_err());
        assert_eq!(read(&source.records, 0).unwrap().saved.zone, 2);
        assert!(destination.refresh().unwrap().is_empty());
        source
            .write(&saved, 12, false, [true, false, false])
            .unwrap();
        assert_eq!(source.mask(), 6);
        assert_eq!(source.pending_boots().unwrap().len(), 1);
        let adopted = destination.refresh().unwrap();
        assert_eq!(adopted.len(), 1);
        assert_eq!(adopted[0].0, 0);
        assert_eq!(adopted[0].1.owner_epoch, epoch + 1);
        assert_eq!(adopted[0].1.experience, 12345);
        assert_eq!(adopted[0].1.inventory, saved[0].inventory);
        assert_eq!(adopted[0].1.reputation, saved[0].reputation);
        assert!(destination.refresh().unwrap().is_empty());
        let mut duplicate = Store::open(directory.to_str().unwrap(), 3).unwrap();
        assert!(duplicate.load().is_err());
        drop(duplicate);
        // A stale source snapshot can still save its other actors, but cannot
        // rewrite the actor now held by the destination.
        let mut at_destination = destination.load().unwrap().saved;
        at_destination[0].experience += 100;
        destination
            .write(&at_destination, 13, false, [false; COUNT])
            .unwrap();
        source.write(&saved, 14, false, [true; COUNT]).unwrap();
        assert_eq!(read(&source.records, 0).unwrap().saved.experience, 12445);
        // Dirty destination death refuses replay and retains the entire record.
        let evidence = fs::read(destination.actor_path(0)).unwrap();
        drop(destination);
        let mut restarted = Store::open(directory.to_str().unwrap(), 3).unwrap();
        assert!(restarted.load().is_err());
        assert_eq!(fs::read(restarted.actor_path(0)).unwrap(), evidence);
        drop(restarted);
        drop(source);
        fs::remove_dir_all(directory).unwrap();
    }
    #[test]
    fn distinct_actor_owners_write_concurrently_without_shared_checkpoint_race() {
        let directory = directory();
        let mut source = Store::open(directory.to_str().unwrap(), 2).unwrap();
        let mut saved = source.load().unwrap().saved;
        progressed(&mut saved[1]);
        saved[1].zone = 3;
        saved[1].previous_zone = 2;
        source.write(&saved, 10, true, [true; COUNT]).unwrap();
        let mut destination = Store::open(directory.to_str().unwrap(), 3).unwrap();
        let destination_saved = destination.load().unwrap().saved;
        let barrier = Arc::new(Barrier::new(2));
        let other_barrier = barrier.clone();
        let a = std::thread::spawn(move || {
            for i in 0..16 {
                saved[0].experience = i;
                barrier.wait();
                source.write(&saved, 20 + i, true, [true; COUNT]).unwrap();
            }
        });
        let b = std::thread::spawn(move || {
            let mut saved = destination_saved;
            for i in 0..16 {
                saved[1].experience = 13000 + i;
                other_barrier.wait();
                destination
                    .write(&saved, 20 + i, true, [true; COUNT])
                    .unwrap();
            }
        });
        a.join().unwrap();
        b.join().unwrap();
        assert_eq!(
            read(&directory.join("actors-v3"), 0)
                .unwrap()
                .saved
                .experience,
            15
        );
        assert_eq!(
            read(&directory.join("actors-v3"), 1)
                .unwrap()
                .saved
                .experience,
            13015
        );
        fs::remove_dir_all(directory).unwrap();
    }
    #[test]
    fn migration_retains_original_and_inhibits_old_controller_before_new_authority() {
        let directory = directory();
        let mut legacy = LegacyStore::open(directory.to_str().unwrap()).unwrap();
        let mut saved = std::array::from_fn(Saved::initial);
        saved[0].hp = 47;
        saved[0].experience = 789;
        legacy.write(&saved, 1, true).unwrap();
        let original = fs::read(directory.join("checkpoint.json")).unwrap();
        drop(legacy);
        let mut store = Store::open(directory.to_str().unwrap(), 2).unwrap();
        assert_eq!(
            fs::read(directory.join("checkpoint.pre-v3.json")).unwrap(),
            original
        );
        assert_eq!(store.load().unwrap().saved[0].hp, 47);
        assert_eq!(store.load().unwrap().saved[0].experience, 789);
        let old = LegacyStore::open(directory.to_str().unwrap()).unwrap();
        assert!(!old.load().unwrap().clean);
        drop(old);
        drop(store);
        fs::remove_dir_all(directory).unwrap();
    }
    #[test]
    fn failed_departure_publication_keeps_source_lease_and_old_state() {
        let directory = directory();
        let mut source = Store::open(directory.to_str().unwrap(), 2).unwrap();
        let mut saved = source.load().unwrap().saved;
        progressed(&mut saved[0]);
        source.write(&saved, 10, false, [false; COUNT]).unwrap();
        let evidence = fs::read(source.actor_path(0)).unwrap();
        saved[0].zone = 3;
        saved[0].previous_zone = 2;
        fs::create_dir(source.temporary_path(0)).unwrap();
        assert!(source.write(&saved, 11, false, [true; COUNT]).is_err());
        assert_eq!(source.mask(), 7);
        assert_eq!(fs::read(source.actor_path(0)).unwrap(), evidence);
        let mut destination = Store::open(directory.to_str().unwrap(), 3).unwrap();
        assert_eq!(destination.load().unwrap().saved[0].zone, 2);
        assert_eq!(destination.mask(), 0);
        drop(destination);
        drop(source);
        fs::remove_dir_all(directory).unwrap();
    }
}
