//! Dungeon Settlers mod launcher core. Steam's install and the player's normal saves are only READ.
#[cfg(unix)]
pub mod copy_mac;
pub mod crypto;
pub mod decide;
pub mod difficulty;
pub mod fetch;
pub mod join;
pub mod flow;
pub mod launch;
pub mod macho;
pub mod model;
pub mod seed;
pub mod steam;
pub mod windows;
pub mod worlds;

/// The side copy of the game for this OS: Mac = `copy_mac` (APFS clone + ad-hoc signature), Windows = `windows`
/// (full copy + hard-linked play copies). Same functions on both.
pub mod copy {
    #[cfg(unix)]
    pub use crate::copy_mac::*;
    #[cfg(windows)]
    pub use crate::windows::*;
}

use anyhow::{Context, Result};
use std::path::{Path, PathBuf};

use model::State;

/// The launcher's own folder:
/// ```text
/// <root>/state.json, index.json(.sig)   pinned build, enabled mods, last index seen
/// <root>/games/<build>/base/...          stock copy of Steam's build (APFS clone on Mac)
/// <root>/games/<build>/play/...          base + mods, ad-hoc signed: what Play starts
/// <root>/cache/<id>/<version>/<file>     downloaded + verified mod files
/// <root>/profiles/<name>/                own saves, logs, Unity home
/// <root>/retired/                        replaced copies (moved here, never deleted by the launcher; Windows:
///                                        a copy the player removes is deleted, see remove_copy)
/// ```
#[derive(Debug, Clone)]
pub struct Paths {
    pub root: PathBuf,
}

impl Paths {
    pub fn new(root: impl Into<PathBuf>) -> Self { Paths { root: root.into() } }

    /// Mac: ~/Library/Application Support/DSModLauncher. Windows: %LOCALAPPDATA%\DSModLauncher. DSML_ROOT overrides (tests).
    pub fn default_root() -> PathBuf {
        if let Some(r) = std::env::var_os("DSML_ROOT") { return r.into(); }
        if cfg!(target_os = "windows") {
            return PathBuf::from(std::env::var_os("LOCALAPPDATA").unwrap_or_default()).join("DSModLauncher");
        }
        PathBuf::from(std::env::var_os("HOME").unwrap_or_default()).join("Library/Application Support/DSModLauncher")
    }

    pub fn state_file(&self) -> PathBuf { self.root.join("state.json") }
    pub fn game_dir(&self, build: &str) -> PathBuf { self.root.join("games").join(build) }
    pub fn cache_dir(&self, id: &str, version: &str) -> PathBuf { self.root.join("cache").join(id).join(version) }
    pub fn profile_dir(&self, name: &str) -> PathBuf { self.root.join("profiles").join(name) }
    pub fn retired(&self) -> PathBuf { self.root.join("retired") }

    pub fn load_state(&self) -> Result<State> {
        match std::fs::read(self.state_file()) {
            Ok(b) => serde_json::from_slice(&b).context("state.json does not parse"),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(State::default()),
            Err(e) => Err(e.into()),
        }
    }

    pub fn save_state(&self, s: &State) -> Result<()> {
        std::fs::create_dir_all(&self.root)?;
        write_atomic(&self.state_file(), &serde_json::to_vec_pretty(s)?)
    }

    /// Builds that have a playable copy on disk.
    pub fn copies(&self) -> Vec<String> {
        let mut v: Vec<String> = std::fs::read_dir(self.root.join("games")).into_iter().flatten().flatten()
            .filter(|e| e.path().join("base.json").exists())
            .filter_map(|e| e.file_name().into_string().ok())
            .collect();
        v.sort();
        v
    }

    /// Move a replaced folder out of the way (never deleted here).
    pub fn retire(&self, p: &Path, label: &str) -> Result<PathBuf> {
        std::fs::create_dir_all(self.retired())?;
        let dst = self.retired().join(format!("{label}-{}", now_secs()));
        std::fs::rename(p, &dst).with_context(|| format!("move {} -> {}", p.display(), dst.display()))?;
        Ok(dst)
    }

    /// A game copy the player chose to remove. Mac: retired (an APFS clone costs almost no disk). Windows: a full
    /// copy costs about 700 MB, so it is deleted (the player said yes; the launcher's own folder only).
    pub fn remove_copy(&self, build: &str) -> Result<()> {
        let d = self.game_dir(build);
        if cfg!(windows) {
            std::fs::remove_dir_all(&d).with_context(|| format!("remove {}", d.display()))?;
        } else {
            self.retire(&d, &format!("{build}-copy"))?;
        }
        Ok(())
    }
}

pub fn write_atomic(p: &Path, data: &[u8]) -> Result<()> {
    let tmp = p.with_extension("tmp-write");
    std::fs::write(&tmp, data)?;
    std::fs::rename(&tmp, p)?;
    Ok(())
}

pub fn now_secs() -> u64 {
    std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0)
}

/// File names from the index must be plain names (no folders, no "..").
pub fn safe_name(n: &str) -> bool {
    !n.is_empty() && n != "." && n != ".." && !n.contains('/') && !n.contains('\\') && !n.contains('\0')
}
