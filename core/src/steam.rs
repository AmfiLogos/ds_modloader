//! Find Steam's install of Dungeon Settlers and read its build id. Steam's files are only READ.
use anyhow::{anyhow, bail, Context, Result};
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

pub const APP_ID: &str = "2798330";
pub const MAC_APP_NAME: &str = "DungeonSettlers.app";

/// Minimal Valve KeyValues (acf / vdf) reader: nested `"key" "value"` and `"key" { ... }`.
#[derive(Debug, Clone, PartialEq)]
pub enum Kv {
    Str(String),
    Obj(BTreeMap<String, Kv>),
}

impl Kv {
    pub fn get(&self, k: &str) -> Option<&Kv> {
        match self { Kv::Obj(m) => m.get(k), _ => None }
    }
    pub fn str(&self) -> Option<&str> {
        match self { Kv::Str(s) => Some(s), _ => None }
    }
}

pub fn parse_kv(text: &str) -> Result<Kv> {
    let toks = tokens(text)?;
    let mut i = 0;
    let root = parse_obj(&toks, &mut i, true)?;
    Ok(Kv::Obj(root))
}

#[derive(Debug, PartialEq)]
enum Tok { S(String), Open, Close }

fn tokens(t: &str) -> Result<Vec<Tok>> {
    let mut out = Vec::new();
    let mut it = t.chars().peekable();
    while let Some(c) = it.next() {
        match c {
            '"' => {
                let mut s = String::new();
                loop {
                    match it.next() {
                        Some('\\') => match it.next() {
                            Some('n') => s.push('\n'),
                            Some('t') => s.push('\t'),
                            Some(x) => s.push(x),
                            None => bail!("unterminated escape"),
                        },
                        Some('"') => break,
                        Some(x) => s.push(x),
                        None => bail!("unterminated string"),
                    }
                }
                out.push(Tok::S(s));
            }
            '{' => out.push(Tok::Open),
            '}' => out.push(Tok::Close),
            '/' if it.peek() == Some(&'/') => { for x in it.by_ref() { if x == '\n' { break; } } }
            c if c.is_whitespace() => {}
            c => bail!("unexpected character {c:?} in vdf"),
        }
    }
    Ok(out)
}

fn parse_obj(t: &[Tok], i: &mut usize, top: bool) -> Result<BTreeMap<String, Kv>> {
    let mut m = BTreeMap::new();
    while *i < t.len() {
        match &t[*i] {
            Tok::Close if !top => { *i += 1; return Ok(m); }
            Tok::S(k) => {
                *i += 1;
                match t.get(*i) {
                    Some(Tok::S(v)) => { m.insert(k.clone(), Kv::Str(v.clone())); *i += 1; }
                    Some(Tok::Open) => { *i += 1; let o = parse_obj(t, i, false)?; m.insert(k.clone(), Kv::Obj(o)); }
                    _ => bail!("key {k:?} has no value"),
                }
            }
            x => bail!("unexpected token {x:?}"),
        }
    }
    if top { Ok(m) } else { bail!("missing closing brace") }
}

/// The steam folder candidates for this OS. DSML_STEAM_ROOTS (a path list, tests) replaces them.
pub fn default_steam_roots() -> Vec<PathBuf> {
    if let Some(r) = std::env::var_os("DSML_STEAM_ROOTS") { return std::env::split_paths(&r).collect(); }
    let mut v = Vec::new();
    if cfg!(target_os = "macos") {
        if let Some(h) = std::env::var_os("HOME") {
            v.push(PathBuf::from(h).join("Library/Application Support/Steam"));
        }
    }
    #[cfg(windows)]
    {
        // registry first (same keys as the friend kit's find_game.bat), then the usual folders on every drive
        v.extend(win::steam_path());
        for d in b'C'..=b'Z' {
            let drive = format!("{}:\\", d as char);
            if !Path::new(&drive).exists() { continue }
            for s in [r"Program Files (x86)\Steam", r"Program Files\Steam", "Steam", "SteamLibrary"] {
                let p = PathBuf::from(&drive).join(s);
                if p.join("steamapps").exists() && !v.contains(&p) { v.push(p) }
            }
        }
    }
    v
}

/// Windows: Steam's registry values (HKCU\Software\Valve\Steam), read only.
#[cfg(windows)]
pub mod win {
    use std::path::PathBuf;
    use winreg::enums::{HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    use winreg::RegKey;

    /// Steam's folder: HKCU SteamPath, else HKLM InstallPath (64- and 32-bit views).
    pub fn steam_path() -> Option<PathBuf> {
        let cu = RegKey::predef(HKEY_CURRENT_USER).open_subkey(r"Software\Valve\Steam").ok()
            .and_then(|k| k.get_value::<String, _>("SteamPath").ok());
        let lm = || ["SOFTWARE\\WOW6432Node\\Valve\\Steam", "SOFTWARE\\Valve\\Steam"].iter().find_map(|k|
            RegKey::predef(HKEY_LOCAL_MACHINE).open_subkey(k).ok().and_then(|k| k.get_value::<String, _>("InstallPath").ok()));
        cu.or_else(lm).filter(|s| !s.is_empty()).map(|s| PathBuf::from(s.replace('/', "\\")))
    }

    /// (Steam's process id, signed-in account id) from HKCU\Software\Valve\Steam\ActiveProcess; 0 = none.
    pub fn active() -> (u32, u32) {
        let k = RegKey::predef(HKEY_CURRENT_USER).open_subkey(r"Software\Valve\Steam\ActiveProcess").ok();
        let get = |n: &str| k.as_ref().and_then(|k| k.get_value::<u32, _>(n).ok()).unwrap_or(0);
        (get("pid"), get("ActiveUser"))
    }

    pub fn process_alive(pid: u32) -> bool {
        use windows_sys::Win32::Foundation::{CloseHandle, STILL_ACTIVE};
        use windows_sys::Win32::System::Threading::{GetExitCodeProcess, OpenProcess, PROCESS_QUERY_LIMITED_INFORMATION};
        if pid == 0 { return false }
        unsafe {
            let h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, pid);
            if h.is_null() { return false }
            let mut code = 0u32;
            let ok = GetExitCodeProcess(h, &mut code) != 0 && code == STILL_ACTIVE as u32;
            CloseHandle(h);
            ok
        }
    }
}

/// What the Play button found about Steam before the game starts.
#[derive(Debug, Clone, PartialEq)]
pub enum SteamStart {
    /// Steam runs with a signed-in user.
    Ready,
    /// Steam was closed: started in the background, ready after this many seconds.
    Started(u64),
    /// No Steam on this computer (the multiplayer mod still runs: it turns the game's Steam check off).
    NotInstalled,
    /// Started or running, but no signed-in user after the wait.
    NotReady,
}

/// Steam's state as the rules below see it: running, signed-in user.
pub struct SteamProbe<'a> {
    pub installed: bool,
    pub state: &'a dyn Fn() -> (bool, bool),
    pub start: &'a dyn Fn() -> bool,
}

/// Branch steamstart's rule (ds_multiplayer rig/mp_launcher.c ensure_steam): Steam running with a user = go on at
/// once; not running = start it in the background and wait until a user is signed in, at most `wait_s` seconds.
pub fn ensure_steam_with(p: &SteamProbe, wait_s: u64, poll: std::time::Duration) -> SteamStart {
    if !p.installed { return SteamStart::NotInstalled }
    let (run, user) = (p.state)();
    if run && user { return SteamStart::Ready }
    if !run && !(p.start)() { return SteamStart::NotReady }
    let t0 = std::time::Instant::now();
    while t0.elapsed().as_secs() < wait_s {
        std::thread::sleep(poll);
        if let (true, true) = (p.state)() { return SteamStart::Started(t0.elapsed().as_secs()) }
    }
    SteamStart::NotReady
}

/// Windows: start Steam (steam.exe -silent: no window) when it is closed, then wait for a signed-in user.
/// DSML_STEAM_WAIT = seconds (default 90).
#[cfg(windows)]
pub fn ensure_steam() -> SteamStart {
    use std::os::windows::process::CommandExt;
    let exe = win::steam_path().map(|p| p.join("steam.exe")).filter(|p| p.exists());
    let wait = std::env::var("DSML_STEAM_WAIT").ok().and_then(|s| s.parse().ok()).unwrap_or(90);
    let state = || { let (pid, user) = win::active(); let run = win::process_alive(pid); (run, run && user != 0) };
    let start = || exe.as_ref().map_or(false, |e| std::process::Command::new(e).arg("-silent")
        .creation_flags(0x0000_0008 | 0x0000_0200)               // DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP
        .stdin(std::process::Stdio::null()).stdout(std::process::Stdio::null()).stderr(std::process::Stdio::null())
        .spawn().is_ok());
    ensure_steam_with(&SteamProbe { installed: exe.is_some(), state: &state, start: &start }, wait, std::time::Duration::from_secs(2))
}

/// Library folders listed in steamapps/libraryfolders.vdf (the main folder first).
pub fn library_folders(steam_root: &Path) -> Vec<PathBuf> {
    let mut v = vec![steam_root.to_path_buf()];
    let f = steam_root.join("steamapps/libraryfolders.vdf");
    if let Ok(t) = std::fs::read_to_string(&f) {
        if let Ok(kv) = parse_kv(&t) {
            if let Some(Kv::Obj(m)) = kv.get("libraryfolders") {
                for e in m.values() {
                    if let Some(p) = e.get("path").and_then(Kv::str) {
                        let p = PathBuf::from(p);
                        if !v.contains(&p) { v.push(p); }
                    }
                }
            }
        }
    }
    v
}

#[derive(Debug, Clone, PartialEq)]
pub struct Install {
    pub library: PathBuf,
    pub build_id: String,
    pub installdir: String,
    /// Mac: .../common/Dungeon Settlers/DungeonSettlers.app. Windows: the game folder.
    pub game_path: PathBuf,
    /// Steam is downloading or staging an update (StateFlags other than 4 = fully installed).
    pub updating: bool,
}

pub fn read_manifest(library: &Path) -> Result<Install> {
    let f = library.join(format!("steamapps/appmanifest_{APP_ID}.acf"));
    let t = std::fs::read_to_string(&f).with_context(|| format!("read {}", f.display()))?;
    let kv = parse_kv(&t)?;
    let st = kv.get("AppState").ok_or_else(|| anyhow!("{}: no AppState", f.display()))?;
    let build = st.get("buildid").and_then(Kv::str).ok_or_else(|| anyhow!("{}: no buildid", f.display()))?;
    let dir = st.get("installdir").and_then(Kv::str).unwrap_or("Dungeon Settlers");
    let flags = st.get("StateFlags").and_then(Kv::str).unwrap_or("4");
    let target = st.get("TargetBuildID").and_then(Kv::str).unwrap_or(build);
    let mut game = library.join("steamapps").join("common").join(dir);
    if cfg!(target_os = "macos") { game = game.join(MAC_APP_NAME); }
    Ok(Install {
        library: library.to_path_buf(),
        build_id: build.to_string(),
        installdir: dir.to_string(),
        game_path: game,
        updating: flags != "4" || (target != build && target != "0"),
    })
}

/// First library that has the game installed.
pub fn find_install(steam_roots: &[PathBuf]) -> Option<Install> {
    for r in steam_roots {
        for lib in library_folders(r) {
            if let Ok(i) = read_manifest(&lib) {
                let ok = if cfg!(windows) { i.game_path.join("DungeonSettlers.exe").exists() } else { i.game_path.exists() };
                if ok { return Some(i); }
            }
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    const ACF: &str = r#""AppState"
{
	"appid"		"2798330"
	"name"		"Dungeon Settlers"
	"StateFlags"		"4"
	"installdir"		"Dungeon Settlers"
	"buildid"		"25846389"
	"TargetBuildID"		"25846389"
	"InstalledDepots"
	{
		"2798332"
		{
			"manifest"		"3084517468227732139"
			"size"		"922595071"
		}
	}
}"#;

    fn lib(name: &str, acf: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("dsml_steam_{}_{}", name, std::process::id()));
        std::fs::create_dir_all(d.join("steamapps/common/Dungeon Settlers/DungeonSettlers.app")).unwrap();
        std::fs::write(d.join(format!("steamapps/appmanifest_{APP_ID}.acf")), acf).unwrap();
        d
    }

    #[test]
    fn parses_real_acf_layout() {
        let kv = parse_kv(ACF).unwrap();
        let st = kv.get("AppState").unwrap();
        assert_eq!(st.get("buildid").unwrap().str(), Some("25846389"));
        assert_eq!(st.get("InstalledDepots").unwrap().get("2798332").unwrap().get("size").unwrap().str(), Some("922595071"));
    }

    #[test]
    fn reads_build_id_and_path() {
        let d = lib("ok", ACF);
        let i = read_manifest(&d).unwrap();
        assert_eq!(i.build_id, "25846389");
        assert!(!i.updating);
        if cfg!(target_os = "macos") {
            assert!(i.game_path.ends_with("steamapps/common/Dungeon Settlers/DungeonSettlers.app"));
        }
    }

    #[test]
    fn update_in_progress_is_seen() {
        let d = lib("upd", &ACF.replace(r#""TargetBuildID"		"25846389""#, r#""TargetBuildID"		"25900000""#));
        assert!(read_manifest(&d).unwrap().updating);
        let d = lib("flags", &ACF.replace(r#""StateFlags"		"4""#, r#""StateFlags"		"1026""#));
        assert!(read_manifest(&d).unwrap().updating);
    }

    #[test]
    fn second_library_is_found() {
        let main = std::env::temp_dir().join(format!("dsml_steam_main_{}", std::process::id()));
        std::fs::create_dir_all(main.join("steamapps")).unwrap();
        let other = lib("other", ACF);
        let vdf = format!(
            "\"libraryfolders\"\n{{\n\t\"0\"\n\t{{\n\t\t\"path\"\t\t\"{}\"\n\t}}\n\t\"1\"\n\t{{\n\t\t\"path\"\t\t\"{}\"\n\t\t\"apps\"\n\t\t{{\n\t\t\t\"2798330\"\t\t\"1\"\n\t\t}}\n\t}}\n}}\n",
            main.display(), other.display());
        std::fs::write(main.join("steamapps/libraryfolders.vdf"), vdf).unwrap();
        let i = find_install(&[main]).unwrap();
        assert_eq!(i.library, other);
    }

    #[test]
    fn steam_start_rules() {
        use std::cell::Cell;
        use std::time::Duration;
        let ms = Duration::from_millis(1);
        let never = || false;
        let p = SteamProbe { installed: false, state: &|| (false, false), start: &never };
        assert_eq!(ensure_steam_with(&p, 1, ms), SteamStart::NotInstalled);
        let started = Cell::new(false);
        let start = || { started.set(true); true };
        let p = SteamProbe { installed: true, state: &|| (true, true), start: &start };
        assert_eq!(ensure_steam_with(&p, 1, ms), SteamStart::Ready);
        assert!(!started.get(), "running Steam is never started again");
        let polls = Cell::new(0);
        let state = || { polls.set(polls.get() + 1); (started.get(), started.get() && polls.get() > 3) };
        let p = SteamProbe { installed: true, state: &state, start: &start };
        assert!(matches!(ensure_steam_with(&p, 5, ms), SteamStart::Started(_)));
        assert!(started.get());
        let p = SteamProbe { installed: true, state: &|| (true, false), start: &never };
        assert_eq!(ensure_steam_with(&p, 0, ms), SteamStart::NotReady, "running, nobody signed in");
        let p = SteamProbe { installed: true, state: &|| (false, false), start: &never };
        assert_eq!(ensure_steam_with(&p, 5, ms), SteamStart::NotReady, "steam.exe did not start");
    }

    #[test]
    fn missing_game_is_none() {
        let d = std::env::temp_dir().join(format!("dsml_steam_none_{}", std::process::id()));
        std::fs::create_dir_all(d.join("steamapps")).unwrap();
        assert!(find_install(&[d]).is_none());
    }
}
