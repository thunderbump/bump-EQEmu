//! Bounded test-fixture observations. This module never changes actor authority.
//! Native callbacks only fill their native buffer; Rust is entered at zone safe points.
use crate::checkpoint;
use actor_core::{inventory::Item, population::Saved};
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use sha2::{Digest, Sha256};
use std::{cell::RefCell, fs, io::Read, path::{Path, PathBuf}};
use std::os::unix::fs::{PermissionsExt};

pub const CONTRACT: &str = "fa89512eeb7071570acd6b3fdc35affa65f9d178c553869e3dce1d5a5d8bfc4a";
const NAME: &str = "central-cdau-integration-v1.json";
const CAPS: [usize; 8] = [2304, 256, 20, 12, 16, 64, 384, 128];
const WIDTHS: [usize; 8] = [196, 128, 128, 235, 128, 144, 272, 236];

#[cxx::bridge(namespace = "eqemu_integration")]
pub mod ffi {
    #[derive(Default, Clone, Copy, Debug, PartialEq, Eq)]
    struct Context {
        actor: u8,
        owner_epoch: u64,
        native_id: u64,
        incarnation: u64,
        epoch: u64,
        generation: u64,
        goal: u64,
        operation: u64,
        race: u32,
        class_id: u32,
        deity: u32,
        zone: u32,
        kind: u8,
        level: u8,
    }
    #[derive(Default, Clone, Copy)]
    struct NativeEvent {
        at_us: u64,
        kind: u8,
        actor: Context,
        other: Context,
        phase: u8,
        reference: u16,
        flags: u32,
        a: u64,
        b: u64,
        c: u64,
        d: u64,
        e: i64,
        f: i64,
        g: i64,
        h: i64,
        x: f64,
        y: f64,
        z: f64,
    }
    #[derive(Default, Clone, Copy)]
    struct RouteFacts {
        id: u32,
        destination: u32,
        x: f64,
        y: f64,
        z: f64,
        arrival_x: f64,
        arrival_y: f64,
        arrival_z: f64,
        flags: u32,
    }
    #[derive(Default, Clone, Copy)]
    struct GeometryFacts {
        actor: Context,
        hostile: Context,
        friendly: Context,
        ax: f64,
        ay: f64,
        az: f64,
        hx: f64,
        hy: f64,
        hz: f64,
        fx: f64,
        fy: f64,
        fz: f64,
        flags: u32,
    }
    extern "Rust" {
        fn observer_open(directory: &str, zone: u32) -> Result<bool>;
        fn observer_native(events: Vec<NativeEvent>, overflow: bool);
        fn observer_poll();
        fn observer_finish();
        fn observer_route(facts: RouteFacts);
        fn observer_geometry(facts: GeometryFacts);
    }
}

// CLOCK_MONOTONIC is the same guest clock used by the native capture buffer.
unsafe extern "C" {
    fn clock_gettime(clock: i32, value: *mut Timespec) -> i32;
}
#[repr(C)]
struct Timespec { seconds: i64, nanos: i64 }
fn now_us() -> u64 {
    let mut value = Timespec { seconds: 0, nanos: 0 };
    // Linux x86-64 bridge only; CLOCK_MONOTONIC and timespec match native ABI.
    if unsafe { clock_gettime(1, &mut value) } != 0 { return 0; }
    u64::try_from(value.seconds).unwrap_or(0).saturating_mul(1_000_000)
        .saturating_add(u64::try_from(value.nanos).unwrap_or(0) / 1000)
}
fn hash_reader(mut reader: impl Read) -> Result<String, String> {
    let mut digest=Sha256::new();
    let mut chunk=[0u8;8192];
    loop {
        let count=reader.read(&mut chunk).map_err(|_|"observer_digest_read")?;
        if count==0 {break;}
        digest.update(&chunk[..count]);
    }
    Ok(format!("{:x}",digest.finalize()))
}
pub fn sha256(bytes: &[u8]) -> Result<String, String> { hash_reader(bytes) }
fn encoded(value: &impl Serialize) -> Result<Vec<u8>, String> {
    serde_json::to_vec(value).map_err(|_| "observer_encode".into())
}
fn decimal(text: &str) -> Option<u64> {
    let value = text.parse::<u64>().ok()?;
    (value.to_string() == text).then_some(value)
}
fn lower_hex(text: &str, size: usize) -> bool {
    text.len() == size && text.bytes().all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b))
}
fn f64_text(value: f64) -> Option<String> {
    let text = value.to_string();
    (value.is_finite() && text.len() <= 32).then_some(text)
}
thread_local! { static TRACE: RefCell<Option<Trace>> = const { RefCell::new(None) }; }
fn with_trace(f: impl FnOnce(&mut Trace)) {
    TRACE.with(|cell| { if let Some(trace) = cell.borrow_mut().as_mut() { f(trace); } });
}
#[derive(Clone, Serialize, Deserialize, Debug, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
struct Request {
    schema: u8,
    run: String,
    process_index: u8,
    session: String,
    request: String,
    op: u8,
    barrier: u16,
}
#[derive(Clone, Serialize)]
struct Ack {
    schema: u8, run: String, process_index: u8, session: String, request: String,
    op: u8, barrier: u16, result: u8, last_seq: usize,
    shard_sha256: String, snapshot_sequence: u16,
}
#[derive(Default, Clone, Copy)]
struct Publication { seq: usize, revision: u64 }
struct Trace {
    directory: PathBuf,
    records: PathBuf,
    run: String,
    case: u8,
    process: u8,
    zone: u32,
    origin: u64,
    session: String,
    descriptor: Value,
    contexts: Vec<ffi::Context>,
    events: Vec<Value>,
    counts: [usize; 8],
    snapshots: Vec<Value>,
    actors: [Value; 3],
    current: [usize; 3],
    revisions: [u64; 3],
    publications: [Publication; 3],
    connection: Value,
    route_flags: u32,
    geometry: Value,
    overflow: bool,
    gaps: u32,
    previous: Option<Request>,
    ack: Option<Vec<u8>>,
    flushes: usize,
    finished: bool,
    awards: [u16; 3],
    previous_bytes: Vec<u8>,
    transfers: [Option<(u32,u32,u64,u16)>;3],
    intent_publications: [Publication;3],
    transfer_count: u16,
}
impl Trace {
    fn new(root: &Path, run: String, case: u8, process: u8, zone: u32, origin: u64,
        descriptor: Value) -> Result<Self, String> {
        let directory = root.join("integration").join(format!("p{process}"));
        fs::create_dir_all(&directory).map_err(|_| "observer_directory")?;
        fs::set_permissions(root.join("integration"), fs::Permissions::from_mode(0o700))
            .and_then(|_| fs::set_permissions(&directory, fs::Permissions::from_mode(0o700)))
            .map_err(|_| "observer_permissions")?;
        if directory.join(NAME).exists() || directory.join("ack.json").exists() {
            return Err("observer_process_index_reused".into());
        }
        let sentinel = ffi::Context { zone, kind: 3, ..ffi::Context::default() };
        Ok(Self { directory, records: root.join("actors-v3"), run, case, process, zone, origin,
            session: String::new(), descriptor, contexts: vec![sentinel], events: Vec::new(),
            counts: [0; 8], snapshots: Vec::new(), actors: std::array::from_fn(|_| json!([0, "0".repeat(64), 0, 0, "0", 0])),
            current: [0; 3], revisions: [0; 3], publications: [Publication::default(); 3],
            connection: json!([false,0,0,0,"0","0","0","0","0","0"]), route_flags: 0,
            geometry: json!([false,0,0,0,"0","0","0","0","0","0","0","0","0",0]),
            overflow: false, gaps: 0, previous: None, ack: None, flushes: 0, finished:false, awards:[0;3], previous_bytes:Vec::new(), transfers:[None;3], intent_publications:[Publication::default();3], transfer_count:0 })
    }
    fn invalid(&mut self) { self.overflow = true; }
    fn context_value(&self, c: &ffi::Context) -> Value {
        json!([self.process,c.actor,c.owner_epoch.to_string(),c.native_id.to_string(),c.incarnation.to_string(),
            c.epoch.to_string(),c.generation.to_string(),c.goal.to_string(),c.operation.to_string(),
            c.race,c.class_id,c.deity,c.zone,c.kind,c.level])
    }
    fn context(&mut self, context: ffi::Context) -> usize {
        if let Some(index) = self.contexts.iter().position(|c| *c == context) {
            if (1..=3).contains(&context.actor) && context.kind!=3 {self.current[usize::from(context.actor)-1]=index;}
            return index;
        }
        if context.actor > 3 || context.kind > 3 || context.zone != self.zone ||
            self.contexts.len() >= 128 || !encoded(&self.context_value(&context)).is_ok_and(|b| b.len()+1 <= 256) {
            self.invalid(); return 0;
        }
        let index = self.contexts.len();
        self.contexts.push(context);
        if (1..=3).contains(&context.actor) && context.kind != 3 {
            self.current[usize::from(context.actor)-1] = index;
        }
        index
    }
    fn event(&mut self, at: u64, kind: u8, context: usize, revision: u64, payload: Value) {
        if self.finished {self.invalid();return;}
        if self.overflow { return; }
        let Some(index) = kind.checked_sub(5).map(usize::from).filter(|i| *i < CAPS.len()) else {
            self.invalid(); return;
        };
        let Some(time) = at.checked_sub(self.origin).filter(|t| *t <= 1_800_000_000) else {
            self.invalid(); return;
        };
        let event = json!([self.process,self.events.len()+1,time,kind,context,revision.to_string(),payload]);
        if self.counts[index] >= CAPS[index] || self.events.len() >= 4096 || context >= self.contexts.len() ||
            !encoded(&event).is_ok_and(|b| b.len()+1 <= WIDTHS[index]) {
            self.invalid(); return;
        }
        self.counts[index] += 1;
        self.events.push(event);
    }
    fn actor_context(&mut self, index: usize, saved: &Saved) -> usize {
        let old = self.contexts[self.current[index]];
        if old.actor == (index+1) as u8 && old.owner_epoch == saved.owner_epoch && old.level == saved.level {
            self.current[index]
        } else {
            self.context(ffi::Context { actor: (index+1) as u8, owner_epoch: saved.owner_epoch,
                zone: self.zone, level: saved.level, ..ffi::Context::default() })
        }
    }
    fn publication(&mut self, actor: usize, saved: &Saved, revision: u64, clean: bool,
        cause: u8, bytes: &[u8]) {
        let Ok(digest) = sha256(bytes) else { self.invalid(); return; };
        let ctx = self.actor_context(actor, saved);
        self.revisions[actor] = revision;
        let mut intent=false;
        if let Some(route)=saved.transfer {
            let same=self.transfers[actor].is_some_and(|(id,zone,epoch,_)|
                id==route.id && zone==route.zone && epoch==saved.owner_epoch);
            if !same {
                if self.transfer_count>=6 {self.invalid();return;}
                self.transfer_count+=1;
                self.transfers[actor]=Some((route.id,route.zone,saved.owner_epoch,self.transfer_count));
                intent=true;
            }
        }
        let token=self.transfers[actor].map(|t|t.3).unwrap_or(0);
        self.event(now_us(),5,ctx,revision,json!([cause,clean,saved.zone,saved.level,
            saved.experience.to_string(),saved.completed.to_string(),token,digest]));
        self.publications[actor] = Publication { seq: self.events.len(), revision };
        if cause==6 {self.intent_publications[actor]=self.publications[actor];}
        if intent {
            self.event(now_us(),11,ctx,revision,json!([4,token,0,saved.zone,0,
                saved.owner_epoch.to_string(),"0","0"]));
        }
        if saved.transfer.is_none() && cause!=7 {self.transfers[actor]=None;}
    }
    fn acknowledgement(&self, request: &Request, result: u8, digest: String, snapshot: u16) -> Ack {
        Ack { schema: 1, run: self.run.clone(), process_index: self.process, session: self.session.clone(),
            request: request.request.clone(), op: request.op, barrier: request.barrier, result,
            last_seq: self.events.len(), shard_sha256: digest, snapshot_sequence: snapshot }
    }
    fn empty_request(&self) -> Request {
        Request { schema:1, run:self.run.clone(), process_index:self.process, session:self.session.clone(),
            request:"0".into(), op:0, barrier:0 }
    }
    fn ack_write(&self, bytes: &[u8]) -> Result<(), String> {
        if bytes.len() > 1024 { return Err("observer_ack_size".into()); }
        checkpoint::atomic(&self.directory,"ack.json",bytes)
    }
    fn shard(&self) -> Value {
        let mut descriptor = self.descriptor.clone();
        descriptor["first_seq"] = json!(usize::from(!self.events.is_empty()));
        descriptor["last_seq"] = json!(self.events.len());
        descriptor["complete_prefix"] = json!(!self.overflow && self.gaps == 0);
        json!({"schema":"central-cdau-integration-v1","contract_sha256":CONTRACT,"run":self.run,
            "case":self.case,"process":descriptor,
            "contexts":self.contexts.iter().enumerate().map(|(i,c)| json!([i,self.context_value(c)])).collect::<Vec<_>>(),
            "events":self.events,"snapshots":self.snapshots,
            "admission":{"actors":self.actors,"connection":self.connection,
                "native_route_flags":self.route_flags,"geometry":self.geometry},
            "complete":!self.overflow && self.gaps==0,"overflow":self.overflow,"gaps":self.gaps})
    }
    fn export(&mut self) -> Result<String,String> {
        let bytes=encoded(&self.shard())?;
        if bytes.len()>851968 {self.invalid();return Err("observer_shard_size".into());}
        checkpoint::atomic(&self.directory,NAME,&bytes)?;
        sha256(&bytes)
    }
    fn snapshot(&self, sequence: u16) -> Result<Value, u8> {
        self.snapshot_with(sequence,|path|read_limited(path,8192))
    }
    fn snapshot_with(&self, sequence:u16, mut read:impl FnMut(&Path)->Result<Vec<u8>,String>)->Result<Value,u8> {
        let mut first = Vec::new();
        let mut hashes = Vec::new();
        let mut sum = 0;
        let mut items = 0;
        for index in 0..3 {
            let bytes = read(&self.records.join(format!("actor-{}.json",index+1))).map_err(|_| 9)?;
            sum += bytes.len();
            if sum > 8192 || !valid_record(&bytes,index) { return Err(9); }
            let record: SnapshotRecord=serde_json::from_slice(&bytes).map_err(|_|9)?;
            let count=record.saved.inventory.as_ref().map_or(0,Vec::len);items+=count;
            if items>8 || record.saved.inventory.as_ref().is_none_or(|items|!fixture_items_valid(items,index)) || record.saved.reputation.len()>20 {return Err(9);}
            hashes.push(sha256(&bytes).map_err(|_| 8)?);
            first.push(String::from_utf8(bytes).map_err(|_| 9)?);
        }
        for (index, original) in first.iter().enumerate() {
            let second = read(&self.records.join(format!("actor-{}.json",index+1))).map_err(|_| 9)?;
            if second != original.as_bytes() { return Err(3); }
        }
        let snapshot = json!({"process_index":self.process,"sequence":sequence,
            "raw_record_sha256_by_actor":hashes,"raw_json_by_actor":first});
        if !encoded(&snapshot).is_ok_and(|b| b.len()+1 <= 11264) { return Err(9); }
        Ok(snapshot)
    }
    fn request(&mut self, bytes: &[u8]) -> Result<Vec<u8>, String> {
        let parsed = serde_json::from_slice::<Request>(bytes).ok().filter(|r| {
            r.schema==1 && r.op<=1 && decimal(&r.request).is_some_and(|n| n>0) &&
                r.session.len()<=48 && !r.session.is_empty() && r.session.bytes().all(|b| b.is_ascii_alphanumeric() || b==b'-')
        });
        let Some(request) = parsed.filter(|_| bytes.len()<=256) else {
            return encoded(&self.acknowledgement(&self.empty_request(),4,"0".repeat(64),0));
        };
        let canonical = encoded(&request)?;
        if bytes != canonical && bytes.strip_suffix(b"\n") != Some(canonical.as_slice()) {
            return encoded(&self.acknowledgement(&request,4,"0".repeat(64),0));
        }
        if request.run!=self.run || request.process_index!=self.process || request.session!=self.session {
            return encoded(&self.acknowledgement(&request,5,"0".repeat(64),0));
        }
        if self.previous.as_ref()==Some(&request) {
            return if bytes==self.previous_bytes { self.ack.clone().ok_or("observer_replay_ack".into()) }
                else {encoded(&self.acknowledgement(&request,6,"0".repeat(64),0))};
        }
        let next = self.previous.as_ref().and_then(|r| decimal(&r.request)).unwrap_or(0).checked_add(1);
        if decimal(&request.request)!=next {
            return encoded(&self.acknowledgement(&request,6,"0".repeat(64),0));
        }
        if self.overflow {
            let digest=self.export()?;
            return encoded(&self.acknowledgement(&request,7,digest,0));
        }
        let mut snapshot = None;
        if request.op==1 {
            if !(1..=12).contains(&request.barrier) || self.snapshots.len()>=12 ||
                self.snapshots.iter().any(|s| s["sequence"]==request.barrier) {
                return encoded(&self.acknowledgement(&request,4,"0".repeat(64),0));
            }
            match self.snapshot(request.barrier) {
                Ok(value) => snapshot=Some(value),
                Err(result) => return encoded(&self.acknowledgement(&request,result,"0".repeat(64),0)),
            }
        } else if self.flushes>=24 {
            self.invalid(); return encoded(&self.acknowledgement(&request,7,"0".repeat(64),0));
        }
        self.event(now_us(),12,0,0,json!([0,self.case,request.request,request.barrier,0,"0","0","0"]));
        if self.overflow { return encoded(&self.acknowledgement(&request,7,"0".repeat(64),0)); }
        if let Some(value)=snapshot { self.snapshots.push(value); }
        if request.op==0 { self.flushes+=1; }
        let digest=self.export()?;
        let result = if request.op==0 { 1 } else { 2 };
        let ack = encoded(&self.acknowledgement(&request,result,digest,if request.op==1 {request.barrier} else {0}))?;
        self.ack_write(&ack)?;
        self.previous_bytes=bytes.to_vec();
        self.previous=Some(request);
        self.ack=Some(ack.clone());
        Ok(ack)
    }
}
fn read_limited(path: &Path, limit: usize) -> Result<Vec<u8>,String> {
    let mut bytes=Vec::new();
    fs::File::open(path).map_err(|_| "observer_read")?.take((limit+1) as u64)
        .read_to_end(&mut bytes).map_err(|_| "observer_read")?;
    if bytes.len()>limit { return Err("observer_size".into()); }
    Ok(bytes)
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct SnapshotRecord { schema: u32, id: usize, profile: String, revision: u64, clean: bool, saved: Saved }
fn valid_record(bytes: &[u8], index: usize) -> bool {
    serde_json::from_slice::<SnapshotRecord>(bytes).is_ok_and(|record| {
        let _ = record.clean;
        record.schema==3 && record.id==index && record.profile==actor_core::population::PROFILE &&
            record.revision>0 && record.saved.valid()
    })
}
fn observer_open(directory: &str, zone: u32) -> Result<bool,String> {
    if std::env::var("EQEMU_CDAU_INTEGRATION").ok().as_deref()!=Some("central-cdau-integration-v1") { return Ok(false); }
    let read = |name| std::env::var(name).map_err(|_| "observer_environment".to_string());
    let run=read("EQEMU_CDAU_RUN")?;
    let process=read("EQEMU_CDAU_PROCESS_INDEX")?;
    let origin=read("EQEMU_CDAU_ORIGIN_US")?;
    let case=match read("EQEMU_CDAU_CASE")?.as_str() { "A"=>0,"B"=>1,"C"=>2,_=>return Err("observer_case".into()) };
    let process=decimal(&process).filter(|n| *n<24).ok_or("observer_process")? as u8;
    let origin=decimal(&origin).filter(|n| *n<=now_us()).ok_or("observer_origin")?;
    if !lower_hex(&run,32) || ![2,4].contains(&zone) { return Err("observer_identity".into()); }
    let metadata=fs::metadata(directory).map_err(|_| "observer_directory")?;
    if !Path::new(directory).is_absolute() || !metadata.is_dir() || metadata.permissions().mode()&0o077!=0 {
        return Err("observer_private_directory".into());
    }
    TRACE.with(|cell| {
        let mut trace=cell.borrow_mut();
        if let Some(old)=trace.as_ref() {
            return if old.run==run && old.process==process && old.zone==zone {Ok(true)} else {Err("observer_rebind".into())};
        }
        let stat=fs::read_to_string("/proc/self/stat").map_err(|_| "observer_process_start")?;
        let start=stat.rsplit_once(')').and_then(|(_,tail)|tail.split_whitespace().nth(19)).ok_or("observer_process_start")?;
        decimal(start).ok_or("observer_process_start")?;
        let binary=hash_reader(fs::File::open("/proc/self/exe").map_err(|_| "observer_executable")?)?;
        let descriptor=json!({"index":process,"native_pid":std::process::id(),"process_start_identity":start,
            "role_world_source_destination":if zone==2 {"source"} else {"destination"},"zone":zone,
            "binary_sha256":binary,"first_seq":0,"last_seq":0,"exit_kind":0,"complete_prefix":true});
        *trace=Some(Trace::new(Path::new(directory),run,case,process,zone,origin,descriptor)?);
        Ok(true)
    })
}
pub fn bind_session(session: &str) {
    with_trace(|trace| {
        if !trace.session.is_empty() { return; }
        if session.is_empty() || session.len()>48 || !session.bytes().all(|b| b.is_ascii_alphanumeric() || b==b'-') {
            trace.invalid(); return;
        }
        trace.session=session.into();
        let ack=encoded(&trace.acknowledgement(&trace.empty_request(),0,"0".repeat(64),0));
        if ack.and_then(|bytes| trace.ack_write(&bytes)).is_err() { trace.invalid(); }
    });
}
fn observer_poll() {
    with_trace(|trace| {
        if trace.session.is_empty() { return; }
        let path=trace.directory.join("request");
        let bytes=match read_limited(&path,256) {
            Ok(bytes)=>bytes,
            Err(_) if !path.exists()=>return,
            Err(_)=> { trace.invalid(); return; }
        };
        if fs::remove_file(path).is_err() { trace.invalid(); return; }
        match trace.request(&bytes) {
            Ok(ack)=> { if trace.ack_write(&ack).is_err() { trace.invalid(); } },
            Err(_)=> { trace.invalid();
                let ack=encoded(&trace.acknowledgement(&trace.empty_request(),8,"0".repeat(64),0));
                if let Ok(bytes)=ack { let _=trace.ack_write(&bytes); }
            }
        }
    });
}
pub fn publication(actor: usize, saved: &Saved, revision: u64, clean: bool, cause: u8, bytes: &[u8]) {
    with_trace(|trace| trace.publication(actor,saved,revision,clean,cause,bytes));
}
pub fn storage_failure(actor:usize) {
    with_trace(|trace|trace.event(now_us(),12,trace.current[actor],trace.revisions[actor],
        json!([3,trace.case,"0",0,1,trace.publications[actor].seq.to_string(),trace.revisions[actor].to_string(),"0"])));
}
pub fn dirty_refusal(revision: u64) {
    with_trace(|trace| trace.event(now_us(),12,0,revision,
        json!([3,trace.case,"0",0,5,"0",revision.to_string(),"0"])));
}
pub fn lease(actor: usize, saved: &Saved, revision: u64, acquired: bool) {
    with_trace(|trace| { let ctx=trace.actor_context(actor,saved);
        trace.event(now_us(),11,ctx,revision,json!([if acquired {6} else {7},trace.transfers[actor].map(|t|t.3).unwrap_or(0),0,saved.zone,0,
            saved.owner_epoch.to_string(),"0","0"])); });
}
pub fn credit(actor: usize, old: &Saved, saved: &Saved) {
    with_trace(|trace| { let ctx=trace.actor_context(actor,saved);let p=trace.publications[actor];let award=trace.awards[actor];
        if award==0 {trace.invalid();return;}
        trace.event(now_us(),8,ctx,p.revision,json!([2,award,trace.process,p.seq.to_string(),
            old.experience.to_string(),saved.experience.to_string(),old.completed.to_string(),saved.completed.to_string()])); });
}
pub fn command(request: u64, verb: &str) {
    with_trace(|trace| trace.event(now_us(),12,0,0,json!([0,trace.case,request.to_string(),
        match verb {"start"=>1,"stop"=>2,"drain"=>3,_=>0},0,"0","0","0"])));
}
pub fn level_request(actor: usize, saved: &Saved) {
    with_trace(|trace| {let ctx=trace.actor_context(actor,saved);
        trace.event(now_us(),9,ctx,trace.revisions[actor],json!([0,saved.level-1,saved.level,"0",saved.experience.to_string()]));});
}
fn fixture_items_valid(items:&[Item], actor:usize)->bool {
    actor_core::inventory::valid(items) && items.len()<=8 && (actor==0 || items.is_empty()) &&
        items.iter().all(|item|item.custom.len()<=64 && item.custom.bytes().all(|b|b.is_ascii_alphanumeric() || b"_^:.-".contains(&b)) &&
            !item.scaling && item.scaling_exp==0 && item.ornament_icon==0 && item.ornament_file==0 && item.ornament_model==0 &&
            item.evolve_id==0 && item.evolve_character==0 && item.evolve_item==0 && item.evolve_final==0 &&
            item.evolve_amount==0 && item.evolve_progress==0.0 && !item.evolve_active && !item.evolve_equipped)
}
pub fn native_body(actor: usize, body: &crate::population::ffi::Body, saved: &Saved) {
    if !body.present || body.inventory_json.is_empty() { return; }
    with_trace(|trace| {
        let Ok(items)=serde_json::from_str::<Vec<Item>>(&body.inventory_json) else {trace.invalid(); return;};
        if !fixture_items_valid(&items,actor) {trace.invalid();return;}
        let digest=encoded(&items).and_then(|b|sha256(&b));
        let Ok(digest)=digest else {trace.invalid();return;};
        let ctx=trace.current[actor];
        let prior=&trace.actors[actor];
        let prior_ctx=prior[0].as_u64().unwrap_or(0) as usize;
        let admitted=trace.contexts.get(prior_ctx).is_some_and(|c|c.kind==1 &&
            c.native_id==trace.contexts[ctx].native_id && c.incarnation==trace.contexts[ctx].incarnation);
        let old_level=prior[3].as_u64().unwrap_or(0);
        if old_level!=0 && old_level!=u64::from(body.level) && saved.level==body.level {
            trace.event(now_us(),9,ctx,trace.revisions[actor],json!([2,old_level,body.level,
                body.next_level_at.to_string(),saved.experience.to_string()]));
        }
        if !admitted {
            trace.event(now_us(),12,ctx,trace.revisions[actor],json!([1,trace.case,"0",103,0,"0","0","0"]));
        }
        trace.actors[actor]=json!([ctx,digest,items.len(),body.level,body.next_level_at.to_string(),body.level_cap]);
    });
}
fn observer_route(facts: ffi::RouteFacts) {
    let ffi::RouteFacts{id,destination,x,y,z,arrival_x,arrival_y,arrival_z,flags}=facts;
    with_trace(|trace| {
        let values=[x,y,z,arrival_x,arrival_y,arrival_z].into_iter().map(f64_text).collect::<Option<Vec<_>>>();
        if let Some(v)=values.filter(|_|flags&!127==0) {
            trace.connection=json!([true,id,trace.zone,destination,v[0],v[1],v[2],v[3],v[4],v[5]]);
            trace.route_flags=flags;
        } else {trace.invalid();}
    });
}
fn observer_geometry(facts: ffi::GeometryFacts) {
    let ffi::GeometryFacts{actor,hostile,friendly,ax,ay,az,hx,hy,hz,fx,fy,fz,flags}=facts;
    with_trace(|trace| {
        let values=[ax,ay,az,hx,hy,hz,fx,fy,fz].into_iter().map(f64_text).collect::<Option<Vec<_>>>();
        let Some(v)=values.filter(|_|flags&!255==0) else {trace.invalid();return;};
        let a=trace.context(actor);let h=trace.context(hostile);let f=trace.context(friendly);
        let value=json!([true,a,h,f,v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],flags]);
        let same_native=|slot:usize,new:usize| trace.geometry[slot].as_u64()
            .and_then(|index|trace.contexts.get(index as usize))
            .is_some_and(|old| old.native_id==trace.contexts[new].native_id && old.incarnation==trace.contexts[new].incarnation);
        if trace.geometry[0]!=true || !same_native(1,a) || !same_native(2,h) || !same_native(3,f) {
            trace.geometry=value;
            trace.event(now_us(),12,a,trace.revisions[usize::from(actor.actor.saturating_sub(1)).min(2)],
                json!([1,trace.case,"0",101,0,"0","0","0"]));
        }
    });
}
fn observer_native(events: Vec<ffi::NativeEvent>, overflow: bool) {
    with_trace(|trace| {
        for event in events {
            let ctx=trace.context(event.actor);let other=trace.context(event.other);
            let actor=usize::from(event.actor.actor.saturating_sub(1)).min(2);
            let revision=trace.revisions[actor];
            let valid=match event.kind {
                6=>event.phase<=1 && event.a<=1 && event.b<=9 && event.c<=3,
                7=>event.reference>0 && event.reference<=4 && [event.e,event.f,event.g].iter().all(|v| i32::try_from(*v).is_ok()),
                8=>event.phase<=1 && event.reference>0 && event.reference<=4 && event.flags<=5,
                9=>event.phase<=2 && event.a<=255 && event.b<=255,
                10=>event.a<=9 && event.flags<=255 && event.b<=u64::from(u32::MAX) &&
                    [event.e,event.f,event.g,event.h].iter().all(|v|i32::try_from(*v).is_ok()),
                11=>event.phase<=8 && event.a<=u64::from(u32::MAX),
                12=>event.phase<=4 && event.b<=12,
                _=>false,
            };
            if !valid {trace.invalid();continue;}
            if event.kind==8 && event.phase==1 {trace.awards[actor]=event.reference;}
            if event.kind==12 && event.reference==102 {
                let coordinates=[event.x,event.y,event.z];
                let matches=coordinates.iter().enumerate().all(|(i,v)|
                    trace.connection[4+i].as_str().and_then(|s|s.parse::<f64>().ok())
                        .is_some_and(|n|n.round()==*v));
                if !matches || event.flags&!7!=0 || trace.connection[0]!=true {trace.invalid();continue;}
                trace.route_flags=event.flags|if trace.connection[1]==9 {8} else {0}|16|32;
            }
            let payload=match event.kind {
                6=>json!([other,event.phase,event.a!=0,event.b,event.c,event.e.to_string(),event.f.to_string()]),
                7=>json!([event.reference,event.e,event.f,event.g]),
                8 if event.phase==0=>json!([0,event.reference,other,ctx,"0","0","0","0"]),
                8 if event.phase==1=>json!([1,event.reference,other,event.a.to_string(),event.b.to_string(),
                    event.c.to_string(),event.d.to_string(),event.flags]),
                9=>json!([event.phase,event.a,event.b,event.c.to_string(),event.d.to_string()]),
                10=>json!([other,event.e,event.f,event.g,event.h,event.a,event.flags,event.b]),
                11 if event.phase==0=> {
                    let Some(v)=[event.x,event.y,event.z].into_iter().map(f64_text).collect::<Option<Vec<_>>>() else {trace.invalid();continue;};
                    json!([event.phase,if event.reference==0 {trace.transfers[actor].map(|t|t.3).unwrap_or(0)} else {event.reference},other,event.a,event.flags,v[0],v[1],v[2]])
                },
                11=>json!([event.phase,if event.reference==0 {trace.transfers[actor].map(|t|t.3).unwrap_or(0)} else {event.reference},other,event.a,event.flags,
                    event.b.to_string(),event.c.to_string(),event.d.to_string()]),
                12=>json!([event.phase,trace.case,event.a.to_string(),event.reference,event.b,
                    (if event.reference==100 {trace.intent_publications[actor].seq as u64} else {event.c}).to_string(),revision.to_string(),"0"]),
                _=> {trace.invalid();continue;}
            };
            trace.event(event.at_us,event.kind,ctx,revision,payload);
        }
        if overflow {trace.invalid();}
    });
}

fn observer_finish() {
    with_trace(|trace| {
        if trace.finished || trace.session.is_empty() {return;}
        trace.finished=true;
        let outcome=(|| -> Result<(),String> {
            let digest=trace.export()?;
            let mut request=trace.empty_request();request.barrier=65535;
            let ack=trace.acknowledgement(&request,if trace.overflow {7} else {1},digest,0);
            trace.ack_write(&encoded(&ack)?)
        })();
        if outcome.is_err() {trace.invalid();}
    });
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::custody::Store;
    use std::sync::atomic::{AtomicU64, Ordering};
    static NEXT: AtomicU64 = AtomicU64::new(0);
    struct Fixture(PathBuf);
    impl Fixture {
        fn new() -> Self {
            let path=std::env::temp_dir().join(format!("cdau-observation-{}-{}",std::process::id(),NEXT.fetch_add(1,Ordering::Relaxed)));
            fs::create_dir(&path).unwrap();
            fs::set_permissions(&path,fs::Permissions::from_mode(0o700)).unwrap();
            let descriptor=json!({"index":0,"native_pid":1,"process_start_identity":"1",
                "role_world_source_destination":"source","zone":2,"binary_sha256":"0".repeat(64),
                "first_seq":0,"last_seq":0,"exit_kind":0,"complete_prefix":true});
            let mut trace=Trace::new(&path,"a".repeat(32),0,0,2,now_us(),descriptor).unwrap();
            trace.session="test-session".into();
            TRACE.with(|cell| *cell.borrow_mut()=Some(trace));
            Self(path)
        }
        fn seed(&self) {
            fs::create_dir(self.0.join("actors-v3")).unwrap();
            for i in 0..3 {
                let mut saved=Saved::initial(i);saved.inventory=Some(Vec::new());
                let bytes=encoded(&json!({"schema":3,"id":i,"profile":"west-qeynos-v1",
                    "revision":1,"clean":true,"saved":saved})).unwrap();
                fs::write(self.0.join(format!("actors-v3/actor-{}.json",i+1)),bytes).unwrap();
            }
        }
    }
    impl Drop for Fixture {
        fn drop(&mut self) {
            TRACE.with(|cell| *cell.borrow_mut()=None);
            fs::remove_dir_all(&self.0).unwrap();
        }
    }
    fn request(id:u64,op:u8,barrier:u16)->Request {
        Request{schema:1,run:"a".repeat(32),process_index:0,session:"test-session".into(),
            request:id.to_string(),op,barrier}
    }
    fn invoke(request:&Request)->Value {
        TRACE.with(|cell|serde_json::from_slice(&cell.borrow_mut().as_mut().unwrap()
            .request(&encoded(request).unwrap()).unwrap()).unwrap())
    }
    fn events()->Vec<Value> {TRACE.with(|cell|cell.borrow().as_ref().unwrap().events.clone())}
    fn native_award(saved:&Saved) {
        let context=ffi::Context{actor:1,owner_epoch:saved.owner_epoch,native_id:17,incarnation:21,
            epoch:1,generation:1,goal:1,operation:1,race:1,class_id:1,deity:140,zone:2,kind:1,level:1};
        observer_native(vec![ffi::NativeEvent{at_us:now_us(),kind:8,actor:context,
            other:ffi::Context{native_id:18,incarnation:22,zone:2,kind:2,level:1,..ffi::Context::default()},
            phase:1,reference:1,a:100,b:500,c:500,d:1000,..ffi::NativeEvent::default()}],false);
    }
    #[test]
    fn sha256_matches_known_vectors_and_streaming_bytes() {
        assert_eq!(sha256(b"").unwrap(),"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        assert_eq!(sha256(b"abc").unwrap(),"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        let million=vec![b'a';1_000_000];
        assert_eq!(sha256(&million).unwrap(),"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
        let bytes=vec![0x5a;32769];
        assert_eq!(hash_reader(std::io::Cursor::new(&bytes)).unwrap(),sha256(&bytes).unwrap());
    }
    #[test]
    fn stale_unknown_noncanonical_and_changed_replay_cannot_emit_or_capture() {
        let _fixture=Fixture::new();
        let mut stale=request(1,0,0);stale.session="other".into();
        assert_eq!(invoke(&stale)["result"],5);
        assert!(events().is_empty());
        let first=invoke(&request(1,0,0));assert_eq!(first["result"],1);
        let count=events().len();assert_eq!(invoke(&request(1,0,0)),first);
        assert_eq!(events().len(),count);
        assert_eq!(invoke(&request(1,1,1))["result"],6);
        let mut unknown=serde_json::to_value(request(2,0,0)).unwrap();unknown["path"]=json!("not-accepted");
        TRACE.with(|cell|{
            let mut trace=cell.borrow_mut();let trace=trace.as_mut().unwrap();
            assert_eq!(serde_json::from_slice::<Value>(&trace.request(&encoded(&unknown).unwrap()).unwrap()).unwrap()["result"],4);
            let mut changed=encoded(&request(1,0,0)).unwrap();changed.push(b'\n');
            assert_eq!(serde_json::from_slice::<Value>(&trace.request(&changed).unwrap()).unwrap()["result"],6);
        });
        assert_eq!(events().len(),count);
    }
    #[test]
    fn snapshot_retains_exact_whole_records_hashes_and_large_u64_and_replay() {
        let fixture=Fixture::new();fixture.seed();
        let path=fixture.0.join("actors-v3/actor-1.json");
        let mut value:Value=serde_json::from_slice(&fs::read(&path).unwrap()).unwrap();
        value["revision"]=json!(u64::MAX);value["saved"]["owner_epoch"]=json!(u64::MAX);
        let raw=encoded(&value).unwrap();fs::write(&path,&raw).unwrap();
        assert_eq!(invoke(&request(1,1,1))["result"],2);
        let shard:Value=serde_json::from_slice(&fs::read(fixture.0.join("integration/p0").join(NAME)).unwrap()).unwrap();
        let snap=&shard["snapshots"][0];
        assert_eq!(snap.as_object().unwrap().len(),4);
        assert_eq!(snap["raw_json_by_actor"][0].as_str().unwrap().as_bytes(),raw);
        assert_eq!(snap["raw_record_sha256_by_actor"][0],sha256(&raw).unwrap());
        assert!(snap.get("raw_json").is_none());
        let count=events().len();assert_eq!(invoke(&request(1,1,1))["result"],2);
        assert_eq!(events().len(),count);
        TRACE.with(|cell|assert_eq!(cell.borrow().as_ref().unwrap().snapshots.len(),1));
    }
    #[test]
    fn observation_does_not_require_or_contend_on_actor_leases() {
        let fixture=Fixture::new();fixture.seed();
        let mut owner=Store::open(fixture.0.to_str().unwrap(),2).unwrap();
        owner.load().unwrap();assert_eq!(owner.mask(),7);
        assert_eq!(invoke(&request(1,1,1))["result"],2);
        assert_eq!(owner.mask(),7);drop(owner);
    }
    #[test]
    fn simultaneous_record_change_is_busy_without_forced_save_or_snapshot() {
        let fixture=Fixture::new();fixture.seed();let mut reads=0;
        TRACE.with(|cell| {
            let trace=cell.borrow();let trace=trace.as_ref().unwrap();
            let result=trace.snapshot_with(1,|path| {
                let mut bytes=fs::read(path).unwrap();reads+=1;
                if reads==4 {
                    let mut value:Value=serde_json::from_slice(&bytes).unwrap();value["revision"]=json!(2);
                    bytes=encoded(&value).unwrap();
                }
                Ok(bytes)
            });
            assert_eq!(result,Err(3));assert!(trace.snapshots.is_empty());assert!(trace.events.is_empty());
        });
        assert_eq!(reads,4);
        let raw=fs::read(fixture.0.join("actors-v3/actor-1.json")).unwrap();
        assert_eq!(serde_json::from_slice::<Value>(&raw).unwrap()["revision"],1);
    }
    #[test]
    fn wrong_schema_and_failed_export_never_publish_success_ack() {
        let fixture=Fixture::new();fixture.seed();
        fs::write(fixture.0.join("actors-v3/actor-2.json"),b"{}").unwrap();
        assert_eq!(invoke(&request(1,1,1))["result"],9);
        assert!(events().is_empty());
        let temp=fixture.0.join("integration/p0").join(format!("{NAME}.tmp"));
        fs::create_dir(temp).unwrap();
        TRACE.with(|cell|assert!(cell.borrow_mut().as_mut().unwrap().request(&encoded(&request(1,0,0)).unwrap()).is_err()));
        assert!(!fixture.0.join("integration/p0/ack.json").exists());
    }
    #[test]
    fn immutable_context_and_overflow_keep_valid_prefix_without_permission_changes() {
        let _fixture=Fixture::new();
        TRACE.with(|cell|{
            let mut borrowed=cell.borrow_mut();let trace=borrowed.as_mut().unwrap();
            let old=ffi::Context{actor:1,zone:2,kind:1,incarnation:u64::MAX,..ffi::Context::default()};
            let original=trace.context(old);let old_value=trace.context_value(&trace.contexts[original]);
            let changed=ffi::Context{generation:1,..old};
            assert_ne!(original,trace.context(changed));assert_eq!(old_value,trace.context_value(&trace.contexts[original]));
            for _ in 0..256 {trace.event(now_us(),6,original,u64::MAX,json!([0,0,false,9,1,i64::MIN.to_string(),i64::MAX.to_string()]));}
            let prefix=trace.events.clone();trace.event(now_us(),6,original,0,json!([0,0,false,0,1,"0","0"]));
            assert!(trace.overflow);assert_eq!(trace.events,prefix);
            assert_eq!(trace.shard()["complete"],false);
        });
    }
    #[test]
    fn credit_commit_survives_later_distinct_actor_write_failure() {
        let fixture=Fixture::new();fixture.seed();
        let mut store=Store::open(fixture.0.to_str().unwrap(),2).unwrap();
        let mut saved=store.load().unwrap().saved;native_award(&saved[0]);
        saved[0].completed=1;saved[0].experience=500;
        fs::create_dir(fixture.0.join("actors-v3/actor-2.json.tmp")).unwrap();
        assert!(store.write(&saved,3,false,[true;3]).is_err());
        let written:Value=serde_json::from_slice(&fs::read(fixture.0.join("actors-v3/actor-1.json")).unwrap()).unwrap();
        assert_eq!(written["saved"]["experience"],500);
        let trace=events();let credit=trace.iter().find(|e|e[3]==8 && e[6][0]==2).unwrap();
        let seq=decimal(credit[6][3].as_str().unwrap()).unwrap() as usize;
        let committed=&trace[seq-1];assert_eq!(committed[3],5);
        assert_eq!(committed[6][0],4);assert_eq!(credit[5],committed[5]);
        assert_eq!(committed[6][7],sha256(&fs::read(fixture.0.join("actors-v3/actor-1.json")).unwrap()).unwrap());
        assert_eq!(credit[6][4],"0");assert_eq!(credit[6][5],"500");
        assert_eq!(trace.iter().filter(|e|e[3]==8 && e[6][0]==2).count(),1);
        TRACE.with(|cell|assert!(!cell.borrow().as_ref().unwrap().overflow));
        drop(store);
    }
    #[test]
    fn failed_same_actor_write_has_no_committed_credit_or_hash() {
        let fixture=Fixture::new();fixture.seed();
        let mut store=Store::open(fixture.0.to_str().unwrap(),2).unwrap();
        let mut saved=store.load().unwrap().saved;native_award(&saved[0]);
        let before=fs::read(fixture.0.join("actors-v3/actor-1.json")).unwrap();
        saved[0].completed=1;saved[0].experience=500;
        let count=events().len();fs::create_dir(fixture.0.join("actors-v3/actor-1.json.tmp")).unwrap();
        assert!(store.write(&saved,3,false,[true;3]).is_err());
        assert_eq!(fs::read(fixture.0.join("actors-v3/actor-1.json")).unwrap(),before);
        let later=events();
        assert!(!later[count..].iter().any(|e|e[3]==8 && e[6][0]==2));
        assert!(!later[count..].iter().any(|e|e[3]==5));
        assert!(later[count..].iter().any(|e|e[3]==12 && e[6][4]==1));
        drop(store);
    }
    #[test]
    fn dirty_refusal_is_typed_and_terminal_export_contains_actual_release() {
        let fixture=Fixture::new();fixture.seed();
        let mut store=Store::open(fixture.0.to_str().unwrap(),2).unwrap();
        let saved=store.load().unwrap().saved;store.write(&saved,3,false,[true;3]).unwrap();
        drop(store);
        let mut retry=Store::open(fixture.0.to_str().unwrap(),2).unwrap();
        assert!(retry.load().is_err());
        assert!(events().iter().any(|e|e[3]==12 && e[4]==0 && e[5]=="3" && e[6][0]==3 && e[6][4]==5 && e[6][6]=="3"));
        drop(retry);observer_finish();
        let ack:Value=serde_json::from_slice(&fs::read(fixture.0.join("integration/p0/ack.json")).unwrap()).unwrap();
        assert_eq!(ack["request"],"0");assert_eq!(ack["barrier"],65535);assert_eq!(ack["result"],1);
        let bytes=fs::read(fixture.0.join("integration/p0").join(NAME)).unwrap();
        assert_eq!(ack["shard_sha256"],sha256(&bytes).unwrap());
        let shard:Value=serde_json::from_slice(&bytes).unwrap();
        assert!(shard["events"].as_array().unwrap().iter().any(|e|e[3]==11 && e[6][0]==7));
        assert_eq!(shard["complete"],true);
    }
    #[test]
    fn fixed_interface_widths_and_hashes_match_selected_source_contract() {
        let selected:Value=serde_json::from_str(include_str!("../protocol/integration-v1.4.json")).unwrap();
        assert_eq!(selected["event_contract_sha256"],CONTRACT);
        assert_eq!(sha256(include_bytes!("../protocol/integration-v1.4.json")).unwrap(),"c604e154baf96908e2d6707714875924a198793438ec219565f05dc4e6cf6eb0");
        assert_eq!(sha256(include_bytes!("../protocol/event-contract-v2.json")).unwrap(),CONTRACT);
        let mut r=request(u64::MAX,1,u16::MAX);r.process_index=23;r.session="s".repeat(48);
        assert!(encoded(&r).unwrap().len()+1<=256);
        let witness:Value=serde_json::from_str(include_str!("../protocol/interface-width-witness.json")).unwrap();
        assert!(encoded(&witness["manifest_max_witness"]).unwrap().len()<=16384);
        assert!(encoded(&witness["summary_max_witness"]).unwrap().len()<=4096);
        assert!(selected["barrier"]["snapshot"].as_str().unwrap().contains("long v2 keys"));
    }
}
