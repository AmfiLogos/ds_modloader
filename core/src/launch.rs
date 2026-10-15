//! Start the play copy with the code mods. Mac: DYLD_INSERT_LIBRARIES, same as rig/mp_launcher.c. Windows: the
//! copy already holds the proxy DLLs (windows.rs); Steam is started first when it is closed (branch steamstart).
use anyhow::{bail, Context, Result};
use std::collections::BTreeMap;
#[cfg(unix)]
use std::os::unix::process::CommandExt;
#[cfg(windows)]
use std::os::windows::process::CommandExt;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};

use crate::model::{this_platform, ModRelease};
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
    /// The pinned game build of this copy (Steam build id). Passed as DSML_BUILD: the loader's build guard runs the
    /// game unmodded when the running build is another one (loader/NOTES.md, future-build guard).
    pub build: String,
    /// Written to <profile>/dsml_session.json; DSML_SESSION points at it.
    pub session: Option<crate::flow::Session>,
}

/// Normal game save folder (only ever read). Windows: %USERPROFILE%\AppData\LocalLow\CanOpener\Dungeon Settlers.
pub fn normal_saves() -> PathBuf {
    if cfg!(windows) {
        return PathBuf::from(std::env::var_os("USERPROFILE").unwrap_or_default()).join(r"AppData\LocalLow\CanOpener\Dungeon Settlers");
    }
    PathBuf::from(std::env::var_os("HOME").unwrap_or_default()).join("Library/Application Support/CanOpener/Dungeon Settlers")
}

/// Folder of the game's code files in a copy: Mac Contents/Frameworks, Windows the game folder.
fn frameworks(app: &Path) -> PathBuf {
    if cfg!(windows) { app.to_path_buf() } else { app.join("Contents/Frameworks") }
}

pub fn env_for(spec: &LaunchSpec) -> Result<BTreeMap<String, String>> {
    let fw = frameworks(&spec.app);
    let fill = |s: &str| s.replace("{profile_dir}", &spec.profile_dir.to_string_lossy())
        .replace("{app}", &spec.app.to_string_lossy())
        .replace("{frameworks}", &fw.to_string_lossy());
    let mut env = BTreeMap::new();
    env.insert("CFFIXED_USER_HOME".into(), spec.profile_dir.join("home").to_string_lossy().into_owned());
    let mut inject = Vec::new();
    for m in &spec.mods {
        let Some(p) = m.platforms.get(this_platform()) else { continue };
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
    // Windows: the proxy DLLs are in the copy already (they load as the game's own DLLs); nothing to inject
    if !inject.is_empty() && cfg!(unix) { env.insert("DYLD_INSERT_LIBRARIES".into(), inject.join(":")); }
    if spec.steam {
        env.insert("SteamAppId".into(), APP_ID.into());
        env.insert("SteamGameId".into(), APP_ID.into());
    }
    env.insert("DSML_MODSET".into(), spec.modset.clone());
    env.insert("DSML_BUILD".into(), spec.build.clone());               // empty = not sent (retain below)
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
        #[cfg(unix)]
        {
            let st = Command::new("/bin/cp").arg("-R").arg(&s).arg(data.join(item)).status()?;
            if !st.success() { bail!("copy of {} failed", s.display()) }
        }
        #[cfg(windows)]
        copy_tree(&s, &data.join(item)).with_context(|| format!("copy of {} failed", s.display()))?;
    }
    std::fs::write(&flag, format!("{}\n", crate::now_secs()))?;
    Ok(())
}

#[cfg(windows)]
fn copy_tree(s: &Path, d: &Path) -> Result<()> {
    if s.is_dir() {
        std::fs::create_dir_all(d)?;
        for e in std::fs::read_dir(s)? { let e = e?; copy_tree(&e.path(), &d.join(e.file_name()))?; }
    } else {
        std::fs::copy(s, d)?;
    }
    Ok(())
}

/// Windows: Steam closed -> start it in the background and wait for a signed-in user (steam.rs ensure_steam).
/// The game starts in every case: the multiplayer mod turns the game's own Steam check off, and the result is
/// written to <profile>/launcher.log.
#[cfg(windows)]
fn steam_first(spec: &LaunchSpec) {
    let r = crate::steam::ensure_steam();
    let line = format!("{} steam before Play: {r:?}\n", crate::now_secs());
    use std::io::Write;
    if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(spec.profile_dir.join("launcher.log")) { let _ = f.write_all(line.as_bytes()); }
}

pub fn launch(spec: &LaunchSpec) -> Result<Child> {
    let exe = spec.app.join(crate::copy::GAME_EXE);
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
    #[cfg(windows)]
    if spec.steam && std::env::var_os("DSML_NO_STEAM_START").is_none() { steam_first(spec); }
    let log = std::fs::OpenOptions::new().create(true).append(true).open(spec.profile_dir.join("stdout.log"))?;
    let mut cmd = Command::new(&exe);
    cmd.arg("-logFile").arg(spec.profile_dir.join("player.log")).args(&spec.extra_args)
        .current_dir(&spec.profile_dir)
        .env_remove("DYLD_INSERT_LIBRARIES")
        .envs(&env)
        .stdin(Stdio::null()).stdout(log.try_clone()?).stderr(log);
    for (k, v) in &spec.extra_env { if v.is_empty() { cmd.env_remove(k); } }
    let nice = spec.nice;
    #[cfg(windows)]
    {
        // own process group, no console; the game outlives the launcher. nice > 0: below normal priority.
        let prio = if nice > 0 { 0x0000_4000 } else { 0 };                 // BELOW_NORMAL_PRIORITY_CLASS
        cmd.creation_flags(0x0000_0008 | 0x0000_0200 | prio);               // DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP
    }
    // own session (the game outlives the launcher); lower priority in the child itself: /usr/bin/nice is
    // SIP-protected and would strip DYLD_INSERT_LIBRARIES
    #[cfg(unix)]
    unsafe {
        cmd.pre_exec(move || {
            libc::setsid();
            if nice != 0 { libc::nice(nice); }
            Ok(())
        });
    }
    cmd.spawn().with_context(|| format!("start {}", exe.display()))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn spec(build: &str) -> LaunchSpec {
        LaunchSpec { app: PathBuf::from("/nonexistent/DungeonSettlers.app"), profile_dir: PathBuf::from("/tmp/p"), saves_from: None,
                     mods: vec![], steam: false, extra_env: BTreeMap::new(), extra_args: vec![], nice: 0, world: None,
                     modset: "m".into(), build: build.into(), session: None }
    }

    #[test]
    fn pinned_build_goes_to_the_game_as_dsml_build() {
        assert_eq!(env_for(&spec("25846389")).unwrap().get("DSML_BUILD").map(String::as_str), Some("25846389"));
        assert!(!env_for(&spec("")).unwrap().contains_key("DSML_BUILD"), "no build known: not sent");
        let mut s = spec("25846389");
        s.extra_env.insert("DSML_BUILD".into(), String::new());
        assert!(!env_for(&s).unwrap().contains_key("DSML_BUILD"), "extra_env can still remove it (tests)");
    }
}
