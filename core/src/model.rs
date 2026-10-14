//! The signed index and the mod manifests it carries.
//! Format: research/plan_launcher.md section 3 ("Manifest"), extended with file urls and sizes.
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

/// Platform keys used in manifests.
pub const MAC_ARM64: &str = "mac-arm64";
pub const WIN_X64: &str = "win-x64";

pub fn this_platform() -> &'static str {
    if cfg!(target_os = "windows") { WIN_X64 } else { MAC_ARM64 }
}

/// `index.json`. Signed as raw bytes; see `crypto::verify_index`.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Index {
    pub schema: u32,
    /// Grows with every published index. The launcher refuses an index older than the last one it saw.
    pub seq: u64,
    #[serde(default)]
    pub issued: String,
    /// Stock files per game build and platform (so the launcher can check Steam's install is stock).
    #[serde(default)]
    pub games: BTreeMap<String, BTreeMap<String, GameStock>>,
    #[serde(default)]
    pub mods: Vec<ModRelease>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Default)]
pub struct GameStock {
    /// Mac: sha256 of the stock Contents/Frameworks/GameAssembly.dylib of this build.
    #[serde(default)]
    pub gameassembly_sha256: String,
    /// Windows: sha256 per relative path (TODO Windows: filled by the publish pipeline).
    #[serde(default)]
    pub files: BTreeMap<String, String>,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum Kind {
    Data,
    Patch,
    Code,
}

/// One immutable mod version (Thunderstore rule: a published version never changes).
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct ModRelease {
    pub id: String,
    pub name: String,
    pub version: String,
    #[serde(default)]
    pub author: String,
    pub kind: Kind,
    #[serde(default = "one")]
    pub api: u32,
    /// gameplay: changes how the game works; every player in a multiplayer game must have it (auto-download,
    /// part of the mod-set hash). client: only changes things for this player; never required from others.
    #[serde(default)]
    pub scope: Scope,
    /// Set when this mod is a custom difficulty (for example Hell).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub difficulty: Option<CustomDifficulty>,
    /// Exact Steam build ids, or "*" for a mod with no per-build data.
    pub game_builds: Vec<String>,
    pub platforms: BTreeMap<String, PlatformPart>,
    #[serde(default)]
    pub requires: Vec<String>,
    #[serde(default)]
    pub conflicts: Vec<String>,
    #[serde(default)]
    pub description: String,
    /// Test-only mods never show in the player's list (the launcher shows them in developer mode only).
    #[serde(default)]
    pub dev_only: bool,
}

fn one() -> u32 { 1 }

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq, Default)]
#[serde(rename_all = "lowercase")]
pub enum Scope {
    /// Default: a mod that does not say otherwise is treated as required from every player.
    #[default]
    Gameplay,
    Client,
}

/// A difficulty step the game does not have. `value` = the campaign's CombatDifficulty number it adds
/// (stock: Easy 0 .. Devastation 4; Hell = 5).
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct CustomDifficulty {
    pub value: u8,
    pub name: String,
    /// Locked behind the same in-game unlock as Devastation (clear Very Hard).
    #[serde(default)]
    pub needs_devastation_unlock: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Default)]
pub struct PlatformPart {
    #[serde(default)]
    pub files: Vec<FileEntry>,
    /// Code mods: file names (from `files`) the loader injects. Today: DYLD_INSERT_LIBRARIES on Mac.
    #[serde(default)]
    pub inject: Vec<String>,
    /// Environment for the game process. Placeholders: {profile_dir} {app} {frameworks}.
    #[serde(default)]
    pub env: BTreeMap<String, String>,
    /// Patch mods: byte patches per game build.
    #[serde(default)]
    pub patches: BTreeMap<String, Vec<BytePatch>>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct FileEntry {
    pub name: String,
    pub sha256: String,
    pub size: u64,
    pub url: String,
    /// Data mods: the stock file this one replaces, as a path inside the app bundle
    /// (for example "Contents/Resources/Data/StreamingAssets/aa/StandaloneOSX/tables.bundle").
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub dest: Option<String>,
}

/// A byte patch in one Mach-O slice of a game file. `expect` must match before the patch is applied
/// (or `new` must already be there), else the copy is refused.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct BytePatch {
    /// Path inside the app bundle, for example "Contents/Frameworks/GameAssembly.dylib".
    pub file: String,
    /// "arm64": offset is relative to the arm64 slice of a fat file (thin file: the file start).
    /// "file": offset is relative to the file start (data files such as catalog.bin).
    #[serde(default = "arm64")]
    pub slice: String,
    /// Hex, for example "0xD25730".
    pub offset: String,
    pub expect: String,
    pub new: String,
}

fn arm64() -> String { "arm64".into() }

impl ModRelease {
    pub fn supports(&self, build: &str, platform: &str) -> bool {
        self.platforms.contains_key(platform)
            && self.game_builds.iter().any(|b| b == build || b == "*")
    }
    pub fn semver(&self) -> semver::Version {
        semver::Version::parse(&self.version).unwrap_or(semver::Version::new(0, 0, 0))
    }
    pub fn key(&self) -> String { format!("{}@{}", self.id, self.version) }
}

/// Mods of one world (one campaign / save). `mods` and `groups` are ids; the difficulty mod is added on top.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Default)]
pub struct WorldProfile {
    #[serde(default)]
    pub name: String,
    #[serde(default)]
    pub mods: Vec<String>,
    /// Names of mod groups (folders) applied to this world.
    #[serde(default)]
    pub groups: Vec<String>,
    /// Custom difficulty mod id (None = a vanilla difficulty, picked in the game).
    #[serde(default)]
    pub custom_difficulty: Option<String>,
    /// The build this world plays on (pinned per world).
    #[serde(default)]
    pub build: Option<String>,
    /// Host rule when this world is hosted: "mods" (Allow mods) or "vanilla" (Vanilla check).
    #[serde(default)]
    pub match_rule: crate::join::MatchRule,
    /// Gameplay mod-set hash of the last Play of this world (the in-game check compares it).
    #[serde(default)]
    pub modset: Option<String>,
}

/// What the launcher keeps: mods per world, groups, pinned builds, last index seq.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Default)]
pub struct State {
    /// The newest build in use (the copy new worlds start on).
    #[serde(default)]
    pub active_build: Option<String>,
    /// The build before it; its copy is kept so the player can still join a friend who has not updated.
    #[serde(default)]
    pub previous_build: Option<String>,
    /// Profile for a NEW world (before the game has made its save). Older states: the global enabled list.
    #[serde(default, alias = "enabled")]
    pub new_world: Vec<String>,
    /// CampaignGuid (from the save header) -> its mods.
    #[serde(default)]
    pub worlds: std::collections::BTreeMap<String, WorldProfile>,
    /// Mod groups: name -> mod ids.
    #[serde(default)]
    pub groups: std::collections::BTreeMap<String, Vec<String>>,
    /// Show custom difficulties (off: only the game's own difficulties).
    #[serde(default)]
    pub custom_difficulties: bool,
    /// Gameplay mod-set hash of the last new-world Play (a world the game makes then gets it).
    #[serde(default)]
    pub new_world_modset: Option<String>,
    #[serde(default)]
    pub last_index_seq: u64,
    #[serde(default)]
    pub dev_mode: bool,
}
