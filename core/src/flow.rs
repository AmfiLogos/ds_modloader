//! The Play path in one place: decide -> download + verify mods -> base copy -> play copy -> new state.
use anyhow::{bail, Result};
use serde::Serialize;
use std::path::PathBuf;

use crate::copy_mac::{build_base, build_play, play_app, play_is_current, set_key, CopyOptions};
use crate::decide::{after_switch, check_set, decide, select, Decision, Plan};
use crate::worlds::{effective_mods, Which};
use crate::fetch::ensure_files;
use crate::model::{Index, ModRelease, State};
use crate::steam::Install;
use crate::Paths;

#[derive(Debug, Clone, Serialize)]
pub struct Ready {
    pub build: String,
    pub app: PathBuf,
    pub mods: Vec<ModRelease>,
    pub state: State,
    /// Gameplay mod-set hash (what the multiplayer check compares).
    pub modset: String,
    /// Copies the player may remove (asked first).
    pub removable: Vec<String>,
}

pub fn current_decision(paths: &Paths, install: Option<&Install>, state: &State, which: &Which, index: &Index,
                        platform: &str) -> Decision {
    decide(install.map(|i| i.build_id.as_str()), install.map_or(false, |i| i.updating), state, which, &paths.copies(), index, platform)
}

/// Make the copy for the build the rules pick, ready to start. Does not start it.
pub fn prepare(paths: &Paths, install: Option<&Install>, state: &State, which: &Which, index: &Index, platform: &str,
               opts: &CopyOptions) -> Result<Ready> {
    let d = current_decision(paths, install, state, which, index, platform);
    let build = match &d.plan {
        Plan::Setup { build } | Plan::Current { build } | Plan::Pinned { build, .. } => build.clone(),
        Plan::Switch { to, .. } => to.clone(),
        Plan::NoGame => bail!("Dungeon Settlers is not installed by Steam on this computer"),
        Plan::SteamUpdating { .. } => bail!("Steam is updating Dungeon Settlers: wait until it is done"),
        Plan::Blocked { build, missing } => bail!("no version for game build {build} yet: {}", missing.join(", ")),
        Plan::Stuck { build, steam_build, waiting_for } => bail!(
            "the copy of build {build} is gone and Steam has {steam_build}; waiting for: {}. Turn these mods off to play.",
            waiting_for.join(", ")),
    };
    let set = select(index, &effective_mods(state, which), &build, platform)
        .map_err(|m| anyhow::anyhow!("no version for build {build}: {}", m.join(", ")))?;
    check_set(&set)?;
    let mut with_dirs = Vec::new();
    for m in &set { with_dirs.push((m.clone(), ensure_files(paths, m, platform)?)); }
    if !paths.game_dir(&build).join("base.json").exists() {
        let i = install.ok_or_else(|| anyhow::anyhow!("Steam's install is needed to make the first copy"))?;
        let stock = index.games.get(&build).and_then(|g| g.get(platform)).cloned().unwrap_or_default();
        build_base(paths, &i.game_path, &i.build_id, &build, &stock, opts)?;
    }
    let app = if play_is_current(paths, &build, &with_dirs, opts) { play_app(paths, &build, &set_key(&build, &with_dirs, opts)) }
              else { build_play(paths, &build, &with_dirs, opts)? };
    let state = after_switch(state, which, &build);
    let removable = crate::decide::removable(&state, &paths.copies());
    let modset = crate::join::modset_hash(&set, &build, platform);
    Ok(Ready { build, app, mods: set, state, modset, removable })
}
