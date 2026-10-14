//! "My units": the player's saved builds and teams, one JSON file (see NOTES.md, "My units file").
//! Each unit keeps the build in the wiki builder's own JSON shape (keys, not link indices), so a stored unit
//! survives a data update that shifts the link indices. Writes are atomic (temp file + rename) and the previous
//! file is kept as `<file>.bak`.
use crate::model::Build;
use serde::{Deserialize, Serialize};
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

pub const FORMAT: &str = "ds-launcher-units";
pub const VERSION: u32 = 1;
/// the builder's team size
pub const TEAM_MAX: usize = crate::codec::MAX_TEAM;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Unit {
    pub id: String,
    pub name: String,
    /// unix milliseconds
    pub created: u64,
    pub updated: u64,
    pub build: Build,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Team {
    pub id: String,
    pub name: String,
    pub unit_ids: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct UnitsFile {
    pub format: String,
    pub version: u32,
    /// the game build of the data the units were made with (informational: units are stored by key)
    pub game_build: String,
    pub units: Vec<Unit>,
    #[serde(default)]
    pub teams: Vec<Team>,
}

impl Default for UnitsFile {
    fn default() -> Self {
        UnitsFile {
            format: FORMAT.into(),
            version: VERSION,
            game_build: crate::data::dsb().game_build().to_string(),
            units: vec![],
            teams: vec![],
        }
    }
}

pub fn now_ms() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_millis() as u64).unwrap_or(0)
}

/// 16 hex characters, unique enough for a local list
pub fn new_id() -> String {
    use std::sync::atomic::{AtomicU64, Ordering};
    static N: AtomicU64 = AtomicU64::new(0);
    let mut seed = [0u8; 8];
    if let Ok(mut f) = fs::File::open("/dev/urandom") {
        use std::io::Read;
        let _ = f.read_exact(&mut seed);
    }
    let mut z = u64::from_le_bytes(seed) ^ (now_ms() << 16) ^ N.fetch_add(1, Ordering::Relaxed).wrapping_mul(0x9E3779B97F4A7C15);
    z = (z ^ (z >> 30)).wrapping_mul(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)).wrapping_mul(0x94D049BB133111EB);
    format!("{:016x}", z ^ (z >> 31))
}

#[derive(Debug)]
pub enum StoreError {
    Io(String),
    Format(String),
    NotFound(String),
    TeamFull,
}
impl std::fmt::Display for StoreError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            StoreError::Io(e) => write!(f, "Could not read or write the units file: {e}"),
            StoreError::Format(e) => write!(f, "The units file is damaged: {e}"),
            StoreError::NotFound(id) => write!(f, "No unit or team with id {id}."),
            StoreError::TeamFull => write!(f, "A team holds at most {TEAM_MAX} units."),
        }
    }
}

pub struct Store {
    pub path: PathBuf,
    pub file: UnitsFile,
}

impl Store {
    /// open (or start) the units file at `path`
    pub fn open(path: &Path) -> Result<Store, StoreError> {
        let file = match fs::read(path) {
            Ok(bytes) => {
                let f: UnitsFile = serde_json::from_slice(&bytes).map_err(|e| StoreError::Format(e.to_string()))?;
                if f.format != FORMAT {
                    return Err(StoreError::Format(format!("format {:?}", f.format)));
                }
                if f.version > VERSION {
                    return Err(StoreError::Format(format!("version {} is newer than this launcher ({VERSION})", f.version)));
                }
                f
            }
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => UnitsFile::default(),
            Err(e) => return Err(StoreError::Io(e.to_string())),
        };
        Ok(Store { path: path.to_path_buf(), file })
    }

    pub fn save(&self) -> Result<(), StoreError> {
        let io = |e: std::io::Error| StoreError::Io(e.to_string());
        if let Some(dir) = self.path.parent() {
            fs::create_dir_all(dir).map_err(io)?;
        }
        let text = serde_json::to_vec_pretty(&self.file).map_err(|e| StoreError::Format(e.to_string()))?;
        let tmp = self.path.with_extension("json.tmp");
        {
            let mut f = fs::File::create(&tmp).map_err(io)?;
            f.write_all(&text).map_err(io)?;
            f.sync_all().map_err(io)?;
        }
        if self.path.exists() {
            let _ = fs::copy(&self.path, self.path.with_extension("json.bak"));
        }
        fs::rename(&tmp, &self.path).map_err(io)
    }

    pub fn get(&self, id: &str) -> Option<&Unit> {
        self.file.units.iter().find(|u| u.id == id)
    }

    /// a name not used by another unit: "Name", "Name 2", "Name 3", ...
    pub fn unique_name(&self, base: &str, except: Option<&str>) -> String {
        let base = base.trim();
        let base = if base.is_empty() { "New Character" } else { base };
        let used = |n: &str| self.file.units.iter().any(|u| u.name == n && Some(u.id.as_str()) != except);
        if !used(base) {
            return base.to_string();
        }
        let mut i = 2;
        while used(&format!("{base} {i}")) {
            i += 1;
        }
        format!("{base} {i}")
    }

    /// insert a new unit (id None) or replace the build of an existing one; the build's name is the unit's name
    pub fn put(&mut self, id: Option<&str>, mut build: Build) -> Result<Unit, StoreError> {
        let t = now_ms();
        match id {
            Some(id) => {
                let name = self.unique_name(build.name.as_deref().unwrap_or(""), Some(id));
                build.name = Some(name.clone());
                let u = self.file.units.iter_mut().find(|u| u.id == id).ok_or_else(|| StoreError::NotFound(id.into()))?;
                u.name = name;
                u.build = build;
                u.updated = t;
                Ok(u.clone())
            }
            None => {
                let name = self.unique_name(build.name.as_deref().unwrap_or(""), None);
                build.name = Some(name.clone());
                let u = Unit { id: new_id(), name, created: t, updated: t, build };
                self.file.units.push(u.clone());
                Ok(u)
            }
        }
    }

    pub fn rename(&mut self, id: &str, name: &str) -> Result<Unit, StoreError> {
        let name = self.unique_name(name, Some(id));
        let u = self.file.units.iter_mut().find(|u| u.id == id).ok_or_else(|| StoreError::NotFound(id.into()))?;
        u.name = name.clone();
        u.build.name = Some(name);
        u.updated = now_ms();
        Ok(u.clone())
    }

    pub fn duplicate(&mut self, id: &str) -> Result<Unit, StoreError> {
        let b = self.get(id).ok_or_else(|| StoreError::NotFound(id.into()))?.build.clone();
        self.put(None, b)
    }

    /// delete a unit (and take it out of every team)
    pub fn delete(&mut self, id: &str) -> Result<(), StoreError> {
        let n = self.file.units.len();
        self.file.units.retain(|u| u.id != id);
        if self.file.units.len() == n {
            return Err(StoreError::NotFound(id.into()));
        }
        for t in &mut self.file.teams {
            t.unit_ids.retain(|u| u != id);
        }
        Ok(())
    }

    /// add every unit of a decoded link; returns the new units
    pub fn import(&mut self, builds: Vec<Build>) -> Result<Vec<Unit>, StoreError> {
        let mut out = vec![];
        for b in builds {
            out.push(self.put(None, b)?);
        }
        Ok(out)
    }

    pub fn team_create(&mut self, name: &str, unit_ids: Vec<String>) -> Result<Team, StoreError> {
        let mut t = Team { id: new_id(), name: self.unique_team_name(name, None), unit_ids: vec![] };
        self.check_members(&unit_ids)?;
        t.unit_ids = unit_ids;
        self.file.teams.push(t.clone());
        Ok(t)
    }
    fn unique_team_name(&self, base: &str, except: Option<&str>) -> String {
        let base = base.trim();
        let base = if base.is_empty() { "Team" } else { base };
        let used = |n: &str| self.file.teams.iter().any(|t| t.name == n && Some(t.id.as_str()) != except);
        if !used(base) {
            return base.to_string();
        }
        let mut i = 2;
        while used(&format!("{base} {i}")) {
            i += 1;
        }
        format!("{base} {i}")
    }
    fn check_members(&self, ids: &[String]) -> Result<(), StoreError> {
        if ids.len() > TEAM_MAX {
            return Err(StoreError::TeamFull);
        }
        for id in ids {
            if self.get(id).is_none() {
                return Err(StoreError::NotFound(id.clone()));
            }
        }
        Ok(())
    }
    /// set a team's name and members (in order)
    pub fn team_update(&mut self, id: &str, name: Option<&str>, unit_ids: Option<Vec<String>>) -> Result<Team, StoreError> {
        if let Some(ids) = &unit_ids {
            self.check_members(ids)?;
        }
        let new_name = name.map(|n| self.unique_team_name(n, Some(id)));
        let t = self.file.teams.iter_mut().find(|t| t.id == id).ok_or_else(|| StoreError::NotFound(id.into()))?;
        if let Some(n) = new_name {
            t.name = n;
        }
        if let Some(ids) = unit_ids {
            t.unit_ids = ids;
        }
        Ok(t.clone())
    }
    pub fn team_delete(&mut self, id: &str) -> Result<(), StoreError> {
        let n = self.file.teams.len();
        self.file.teams.retain(|t| t.id != id);
        if n == self.file.teams.len() {
            Err(StoreError::NotFound(id.into()))
        } else {
            Ok(())
        }
    }
    /// the builds of a team, in order
    pub fn team_builds(&self, id: &str) -> Result<Vec<Build>, StoreError> {
        let t = self.file.teams.iter().find(|t| t.id == id).ok_or_else(|| StoreError::NotFound(id.into()))?;
        Ok(t.unit_ids.iter().filter_map(|u| self.get(u)).map(|u| u.build.clone()).collect())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trip_and_teams() {
        let dir = std::env::temp_dir().join(format!("dsub_store_{}", new_id()));
        let path = dir.join("units.json");
        let mut s = Store::open(&path).unwrap();
        let mut b = Build::default_build();
        b.name = Some("Lowell".into());
        let a = s.put(None, b.clone()).unwrap();
        let c = s.put(None, b.clone()).unwrap();
        assert_eq!(c.name, "Lowell 2");
        let t = s.team_create("Raid", vec![a.id.clone(), c.id.clone()]).unwrap();
        s.save().unwrap();
        let s2 = Store::open(&path).unwrap();
        assert_eq!(s2.file, s.file);
        let mut s3 = s2;
        s3.delete(&a.id).unwrap();
        assert_eq!(s3.file.teams[0].unit_ids, vec![c.id.clone()]);
        assert!(s3.team_update(&t.id, None, Some(vec!["nope".into()])).is_err());
        s3.save().unwrap();
        assert!(path.with_extension("json.bak").exists());
        let _ = fs::remove_dir_all(&dir);
    }
}
