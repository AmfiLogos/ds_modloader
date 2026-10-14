//! Start the play copy with the code mods (Mac: DYLD_INSERT_LIBRARIES, same as rig/mp_launcher.c).
use anyhow::{bail, Context, Result};
use std::collections::BTreeMap;
use std::os::unix::process::CommandExt;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};

use crate::model::{ModRelease, MAC_ARM64};
use crate::steam::APP_ID;

#[derive(Debug, Clone)]
pub struct LaunchSpec {
    pub app: PathBuf,
    /// Own saves, Unity home, logs (DSRIG_DIR for the multiplayer mod).
    pub profile_dir: PathBuf,
    /// First run only: copy the saves from here (READ only). None = start with no saves.
    pub saves_from: Option<PathBuf>,
    pub mods: Vec<ModRelease>,
    /// Steam on: steam_appid.txt + SteamAppId, so the game runs from the copy without a restart via Steam.
    pub steam: bool,
    /// Extra environment; an empty value removes the variable (tests: hidden launch).
    pub extra_env: BTreeMap<String, String>,
    pub extra_args: Vec<String>,
    pub nice: i32,
    /// The world this Play is for (None = a new world) and its gameplay mod-set hash. Passed to the game as
    /// DSML_WORLD / DSML_MODSET so a loader can refuse a world loaded with other mods (loader step 2).
    pub world: Option<String>,
    pub modset: String,
    /// Written to <profile>/dsml_session.json; DSML_SESSION points at it.
    pub session: Option<crate::flow::Session>,
}

/// Normal game save folder (only ever read).
pub fn normal_saves() -> PathBuf {
    PathBuf::from(std::env::var_os("HOME").unwrap_or_default()).join("Library/Application Support/CanOpener/Dungeon Settlers")
}

pub fn env_for(spec: &LaunchSpec) -> Result<BTreeMap<String, String>> {
    let fw = spec.app.join("Contents/Frameworks");
    let fill = |s: &str| s.replace("{profile_dir}", &spec.profile_dir.to_string_lossy())
        .replace("{app}", &spec.app.to_string_lossy())
        .replace("{frameworks}", &fw.to_string_lossy());
    let mut env = BTreeMap::new();
    env.insert("CFFIXED_USER_HOME".into(), spec.profile_dir.join("home").to_string_lossy().into_owned());
    let mut inject = Vec::new();
    for m in &spec.mods {
        let Some(p) = m.platforms.get(MAC_ARM64) else { continue };
        for (k, v) in &p.env {
            if let Some(old) = env.insert(k.clone(), fill(v)) {
                if old != fill(v) { bail!("{} sets {k} to a different value than another mod", m.name) }
            }
        }
        for i in &p.inject {
            let f = fw.join(i);
            if !f.exists() { bail!("{}: {} is not in the copy", m.name, i) }
            inject.push(f.to_string_lossy().into_owned());
        }
    }
    if !inject.is_empty() { env.insert("DYLD_INSERT_LIBRARIES".into(), inject.join(":")); }
    if spec.steam {
        env.insert("SteamAppId".into(), APP_ID.into());
        env.insert("SteamGameId".into(), APP_ID.into());
    }
    env.insert("DSML_MODSET".into(), spec.modset.clone());
    if let Some(w) = &spec.world { env.insert("DSML_WORLD".into(), w.clone()); }
    if let Some(s) = &spec.session {
        env.insert("DSML_SESSION".into(), spec.profile_dir.join("dsml_session.json").to_string_lossy().into_owned());
        env.insert("DSML_RULE".into(), serde_json::to_value(s.rule)?.as_str().unwrap_or("mods").to_string());
    }
    for (k, v) in &spec.extra_env { env.insert(k.clone(), v.clone()); }
    env.retain(|_, v| !v.is_empty());
    Ok(env)
}

fn copy_first_saves(from: &Path, data: &Path) -> Result<()> {
    let flag = data.join(".copied_from_normal_game");
    if flag.exists() { return Ok(()) }
    for item in ["Saves", "OutGame.json", "UserSetting.json", "UserCampaignLog.json"] {
        let s = from.join(item);
        if !s.exists() { continue }
        let st = Command::new("/bin/cp").arg("-R").arg(&s).arg(data.join(item)).status()?;
        if !st.success() { bail!("copy of {} failed", s.display()) }
    }
    std::fs::write(&flag, format!("{}\n", crate::now_secs()))?;
    Ok(())
}

pub fn launch(spec: &LaunchSpec) -> Result<Child> {
    let exe = spec.app.join(crate::copy_mac::GAME_EXE);
    if !exe.exists() { bail!("no game in {}", spec.app.display()) }
    let data = spec.profile_dir.join("data");
    std::fs::create_dir_all(&data)?;
    std::fs::create_dir_all(spec.profile_dir.join("home"))?;
    if let Some(src) = &spec.saves_from { copy_first_saves(src, &data)?; }
    if spec.steam { std::fs::write(spec.profile_dir.join("steam_appid.txt"), format!("{APP_ID}\n"))?; }
    if let Some(s) = &spec.session {
        crate::write_atomic(&spec.profile_dir.join("dsml_session.json"), &serde_json::to_vec_pretty(s)?)?;
    }
    let env = env_for(spec)?;
    let log = std::fs::OpenOptions::new().create(true).append(true).open(spec.profile_dir.join("stdout.log"))?;
    let mut cmd = Command::new(&exe);
    cmd.arg("-logFile").arg(spec.profile_dir.join("player.log")).args(&spec.extra_args)
        .current_dir(&spec.profile_dir)
        .env_remove("DYLD_INSERT_LIBRARIES")
        .envs(&env)
        .stdin(Stdio::null()).stdout(log.try_clone()?).stderr(log);
    for (k, v) in &spec.extra_env { if v.is_empty() { cmd.env_remove(k); } }
    let nice = spec.nice;
    // own session (the game outlives the launcher); lower priority in the child itself: /usr/bin/nice is
    // SIP-protected and would strip DYLD_INSERT_LIBRARIES
    unsafe {
        cmd.pre_exec(move || {
            libc::setsid();
            if nice != 0 { libc::nice(nice); }
            Ok(())
        });
    }
    cmd.spawn().with_context(|| format!("start {}", exe.display()))
}
