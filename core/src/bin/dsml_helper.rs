//! dsml-helper: the work "Dungeon Settlers Modded.app" cannot do in its C stub. No window.
//!   dsml-helper prepare --root <ROOT> [--icon <AppIcon.icns>] [--starter <dsml-start>]
//!     Makes the stock base copy of Steam's build (APFS clone, checked against Steam's seal) and the play copy
//!     the stub starts (bundle id com.CanOpener.Dungeon-Settlers.Modded, the app icon, the starter as its
//!     CFBundleExecutable (copy_mac::install_starter), ad-hoc signed). Icon and starter: default = the app's own.
//!     stdout: "BUILD <id>" and "PLAY <path>" as the last two lines. Already present = printed at once.
//!     Error: last line "ERROR <message for the player>", exit 1.
//!   dsml-helper ident --app <play copy> [--starter <dsml-start>]
//!     Gives a copy made before copy_mac::own_identity (Local Network privacy) its own identity, in place, puts the
//!     starter in (given), signs it again and tells LaunchServices (its record holds the old executable).
//!     stdout: "IDENT <COPY_IDENT>" when done (also when nothing had to change).
//! Steam's install is only READ. Replaced folders are moved to <ROOT>/retired, never deleted.
use anyhow::{anyhow, bail, Context, Result};
use dsml_core::copy_mac::{self, CopyOptions};
use dsml_core::model::GameStock;
use dsml_core::{crypto, now_secs, steam, write_atomic, Paths};
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};
use std::process::Command;

const BUNDLE_ID: &str = "com.CanOpener.Dungeon-Settlers.Modded";
const NAME: &str = "Dungeon Settlers Modded";
/// The game developer's Apple team: a GameAssembly signed by it is Steam's stock file.
const TEAM: &str = "2DKWCPLJT6";
/// sha256 of the stock Contents/Frameworks/GameAssembly.dylib per known build (checked: codesign -v passes,
/// TeamIdentifier 2DKWCPLJT6, arm64 UUID 3C5DD8BD-E371-3C0E-A674-A81042B0AC9F for 25846389).
const KNOWN_STOCK: &[(&str, &str)] = &[
    ("25846389", "e50f729b63df79415b6b3e5753943ad186cba282bc058c739d01e409b3a76876"),
];
const PLAY_DIR: &str = "play-modded";
const LSREGISTER: &str = "/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister";

#[derive(Debug, Serialize, Deserialize, PartialEq)]
struct PlayMarker {
    build: String,
    bundle_id: String,
    icon_sha256: String,
    #[serde(default)]
    ident: u32,
    /// sha256 of the starter put in (the app's dsml-start), "" = none
    #[serde(default)]
    starter_sha256: String,
    created: u64,
}

fn run(cmd: &mut Command) -> Result<String> {
    let out = cmd.output().with_context(|| format!("start {:?}", cmd))?;
    if !out.status.success() {
        bail!("{:?} failed: {}", cmd, String::from_utf8_lossy(&out.stderr).trim());
    }
    Ok(String::from_utf8_lossy(&out.stdout).into_owned())
}

/// Steam-signed by the game's developer (stock file, not a modded one).
fn steam_signed(p: &Path) -> bool {
    let ok = Command::new("/usr/bin/codesign").arg("-v").arg(p).output().map(|o| o.status.success()).unwrap_or(false);
    if !ok { return false }
    Command::new("/usr/bin/codesign").arg("-dv").arg(p).output()
        .map(|o| String::from_utf8_lossy(&o.stderr).lines().any(|l| l.trim() == format!("TeamIdentifier={TEAM}")))
        .unwrap_or(false)
}

/// The stock GameAssembly checksum of `build`: built-in table, else Steam's live file when it carries the
/// developer's own signature (a build we do not know yet: the loader then runs it unmodded).
fn game_stock(steam_app: &Path, build: &str) -> Result<GameStock> {
    if let Some((_, h)) = KNOWN_STOCK.iter().find(|(b, _)| *b == build) {
        return Ok(GameStock { gameassembly_sha256: h.to_string(), ..Default::default() });
    }
    let fw = steam_app.join("Contents/Frameworks");
    for c in [fw.join("GameAssembly.dylib"), fw.join(format!("GameAssembly.dylib.stock_{build}"))] {
        if c.exists() && steam_signed(&c) {
            eprintln!("build {build} is new to this helper: using Steam's signed {}", c.display());
            return Ok(GameStock { gameassembly_sha256: crypto::sha256_file(&c)?, ..Default::default() });
        }
    }
    bail!("the game files of build {build} are changed (GameAssembly is not the one Steam installed). \
           Use Steam > Dungeon Settlers > Properties > Installed Files > Verify integrity of game files, then try again.")
}

fn play_app(paths: &Paths, build: &str) -> PathBuf { paths.game_dir(build).join(PLAY_DIR).join("DungeonSettlers.app") }

fn marker_path(paths: &Paths, build: &str) -> PathBuf { paths.game_dir(build).join(format!("{PLAY_DIR}.json")) }

fn icon_sha(icon: Option<&Path>) -> Result<String> {
    Ok(match icon { Some(p) => crypto::sha256_file(p)?, None => String::new() })
}

fn play_is_complete(paths: &Paths, build: &str, icon: Option<&Path>, starter: Option<&Path>) -> bool {
    let Ok(b) = std::fs::read(marker_path(paths, build)) else { return false };
    let Ok(m) = serde_json::from_slice::<PlayMarker>(&b) else { return false };
    m.build == build && m.bundle_id == BUNDLE_ID && m.ident == copy_mac::COPY_IDENT && Some(m.icon_sha256) == icon_sha(icon).ok()
        && Some(m.starter_sha256) == icon_sha(starter).ok()
        && play_app(paths, build).join(copy_mac::GAME_EXE).exists()
        && paths.game_dir(build).join("base.json").exists()
}

/// Play copy = clone of base + own name, bundle id and icon, then ad-hoc signatures (as copy_mac::build_play
/// with no mods; the icon must go in BEFORE the signature, which build_play does not offer).
fn build_play(paths: &Paths, build: &str, icon: Option<&Path>, starter: Option<&Path>) -> Result<PathBuf> {
    let gd = paths.game_dir(build);
    let base = copy_mac::base_app(paths, build);
    let tmp = gd.join(format!("{PLAY_DIR}.building"));
    if tmp.exists() { paths.retire(&tmp, &format!("{build}-{PLAY_DIR}.building"))?; }
    std::fs::create_dir_all(&tmp)?;
    let app = tmp.join("DungeonSettlers.app");
    run(Command::new("/bin/cp").arg("-c").arg("-R").arg(&base).arg(&app))?;
    let c = app.join("Contents");
    let pl = c.join("Info.plist");
    run(Command::new("/usr/bin/plutil").args(["-replace", "CFBundleIdentifier", "-string", BUNDLE_ID]).arg(&pl))?;
    run(Command::new("/usr/bin/plutil").args(["-replace", "CFBundleName", "-string", NAME]).arg(&pl))?;
    if let Some(i) = icon {
        let dst = c.join("Resources/AppIcon.icns");
        if dst.exists() { bail!("the game itself has Resources/AppIcon.icns: not replaced") }
        std::fs::copy(i, &dst)?;
        run(Command::new("/usr/bin/plutil").args(["-replace", "CFBundleIconFile", "-string", "AppIcon"]).arg(&pl))?;
    }
    if let Some(s) = starter { copy_mac::install_starter(&app, &std::fs::read(s)?, BUNDLE_ID)?; }
    copy_mac::own_identity(&app, BUNDLE_ID)?;
    run(Command::new("/usr/bin/xattr").arg("-cr").arg(&app))?;
    sign(&app, BUNDLE_ID)?;

    let pd = gd.join(PLAY_DIR);
    if pd.exists() { paths.retire(&pd, &format!("{build}-{PLAY_DIR}"))?; }
    std::fs::rename(&tmp, &pd)?;
    if starter.is_some() { ls_refresh(&play_app(paths, build)); }   // a copy before it at this path may be registered
    let m = PlayMarker { build: build.into(), bundle_id: BUNDLE_ID.into(), icon_sha256: icon_sha(icon)?,
                         ident: copy_mac::COPY_IDENT, starter_sha256: icon_sha(starter)?, created: now_secs() };
    write_atomic(&marker_path(paths, build), &serde_json::to_vec_pretty(&m)?)?;
    Ok(play_app(paths, build))
}

/// The game binary loses Steam's hardened runtime so DYLD_INSERT_LIBRARIES can load the loader. Its identifier stays
/// the bundle id also when the starter is the bundle's main executable (signed alone, it would be its file name).
fn sign(app: &Path, bundle_id: &str) -> Result<()> {
    run(Command::new("/usr/bin/codesign").args(["-f", "-s", "-", "-i", bundle_id]).arg(app.join(copy_mac::GAME_EXE)))?;
    run(Command::new("/usr/bin/nice").args(["-n", "10", "/usr/bin/codesign", "-f", "-s", "-"]).arg(app))?;
    run(Command::new("/usr/bin/codesign").arg("-v").arg(app)).context("signature check of the new copy")?;
    Ok(())
}

/// LaunchServices keeps the executable it read when it registered the copy (the game binary before the starter): a Dock
/// click would still start that. The copy is registered again.
fn ls_refresh(app: &Path) {
    let _ = Command::new("/usr/bin/touch").arg(app).status();
    let _ = Command::new(LSREGISTER).arg("-f").arg(app).output();
}

/// `ident`: a pinned copy the stub keeps (copy.json) gets its own identity and the starter in place; its marker follows.
/// The stub runs it only while no game of its install runs (the game binary is signed again).
fn ident(app: &Path, starter: Option<&Path>) -> Result<()> {
    let pl = plist::Value::from_file(app.join("Contents/Info.plist")).context("read the copy's Info.plist")?;
    let id = pl.as_dictionary().and_then(|d| d.get("CFBundleIdentifier")).and_then(|v| v.as_string())
        .ok_or_else(|| anyhow!("the copy has no bundle id"))?.to_string();
    if let Some(s) = starter { copy_mac::install_starter(app, &std::fs::read(s)?, &id)?; }
    let changed = copy_mac::own_identity(app, &id)? || starter.is_some();
    // also when a former run changed the copy but could not sign it
    if changed || !Command::new("/usr/bin/codesign").arg("-v").arg(app).output().map(|o| o.status.success()).unwrap_or(false) {
        sign(app, &id)?;
    }
    if starter.is_some() { ls_refresh(app); }
    let mp = app.parent().and_then(|d| d.parent()).map(|g| g.join(format!("{PLAY_DIR}.json")));
    if let Some(mp) = mp.filter(|p| p.exists() && app.parent().map(|d| d.ends_with(PLAY_DIR)).unwrap_or(false)) {
        if let Ok(mut m) = serde_json::from_slice::<PlayMarker>(&std::fs::read(&mp)?) {
            let ss = icon_sha(starter)?;
            if m.ident != copy_mac::COPY_IDENT || (starter.is_some() && m.starter_sha256 != ss) {
                m.ident = copy_mac::COPY_IDENT;
                if starter.is_some() { m.starter_sha256 = ss; }
                write_atomic(&mp, &serde_json::to_vec_pretty(&m)?)?;
            }
        }
    }
    Ok(())
}

/// Folders with verified stock copies of resource files (a file is used only when it equals Steam's seal).
fn extra_stock_dirs() -> Vec<PathBuf> {
    let mut v: Vec<PathBuf> = std::env::var_os("DSML_STOCK_DIRS")
        .map(|s| std::env::split_paths(&s).collect()).unwrap_or_default();
    if let Some(h) = std::env::var_os("HOME") {
        // the owner's Mac: his live install carries mod-changed assets (same folder as make_mp_app.sh)
        let d = PathBuf::from(h).join("ds_weaponslot/b25846389/assets/work/stock_aa");
        if d.is_dir() { v.push(d) }
    }
    v
}

fn default_icon() -> Option<PathBuf> {
    let exe = std::env::current_exe().ok()?.canonicalize().ok()?;
    let p = exe.parent()?.parent()?.join("Resources/AppIcon.icns");
    p.exists().then_some(p)
}

/// The app's own starter, next to this helper (Contents/MacOS/dsml-start)
fn default_starter() -> Option<PathBuf> {
    let exe = std::env::current_exe().ok()?.canonicalize().ok()?;
    let p = exe.parent()?.join(copy_mac::STARTER);
    p.exists().then_some(p)
}

fn prepare(root: &Path, icon: Option<&Path>, starter: Option<&Path>) -> Result<(String, PathBuf)> {
    let paths = Paths::new(root);
    let inst = steam::find_install(&steam::default_steam_roots()).ok_or_else(|| anyhow!(
        "Dungeon Settlers is not installed by Steam on this Mac. Install it in Steam, then open Dungeon Settlers Modded again."))?;
    let build = inst.build_id.clone();
    if play_is_complete(&paths, &build, icon, starter) { return Ok((build.clone(), play_app(&paths, &build))) }
    if inst.updating {
        bail!("Steam is updating Dungeon Settlers. Wait until the update is done, then open Dungeon Settlers Modded again.");
    }
    let stock = game_stock(&inst.game_path, &build)?;
    let opts = CopyOptions { bundle_id: BUNDLE_ID.into(), name: NAME.into(), background_only: false,
                             extra_stock_dirs: extra_stock_dirs() };
    copy_mac::build_base(&paths, &inst.game_path, &build, &build, &stock, &opts).map_err(|e| {
        let s = format!("{e:#}");
        if s.contains("No space left") { anyhow!("Not enough free disk space to copy the game. Free some space, then try again.") }
        else { e }
    })?;
    let app = build_play(&paths, &build, icon, starter)?;
    Ok((build, app))
}

fn main() {
    let a: Vec<String> = std::env::args().skip(1).collect();
    let mut root: Option<PathBuf> = None;
    let mut icon: Option<PathBuf> = None;
    let mut starter: Option<PathBuf> = None;
    let mut app: Option<PathBuf> = None;
    let mut i = 1;
    while i < a.len() {
        match (a[i].as_str(), a.get(i + 1)) {
            ("--root", Some(v)) => { root = Some(v.into()); i += 2 }
            ("--icon", Some(v)) => { icon = Some(v.into()); i += 2 }
            ("--starter", Some(v)) => { starter = Some(v.into()); i += 2 }
            ("--app", Some(v)) => { app = Some(v.into()); i += 2 }
            _ => { root = None; app = None; break }
        }
    }
    if a.first().map(String::as_str) == Some("ident") {
        let Some(app) = app else {
            println!("ERROR usage: dsml-helper ident --app <play copy> [--starter <dsml-start>]");
            std::process::exit(2);
        };
        match ident(&app, starter.as_deref()) {
            Ok(()) => println!("IDENT {}", copy_mac::COPY_IDENT),
            Err(e) => { eprintln!("dsml-helper: {e:#}"); println!("ERROR {}", format!("{e}").replace('\n', " ")); std::process::exit(1); }
        }
        return;
    }
    if a.first().map(String::as_str) != Some("prepare") || root.is_none() {
        println!("ERROR usage: dsml-helper prepare --root <ROOT> [--icon <AppIcon.icns>] [--starter <dsml-start>]");
        std::process::exit(2);
    }
    let icon = icon.or_else(default_icon);
    let starter = starter.or_else(default_starter);
    match prepare(root.as_deref().unwrap(), icon.as_deref(), starter.as_deref()) {
        Ok((build, app)) => {
            println!("BUILD {build}");
            println!("PLAY {}", app.display());
        }
        Err(e) => {
            eprintln!("dsml-helper: {e:#}");
            println!("ERROR {}", format!("{e}").replace('\n', " "));
            std::process::exit(1);
        }
    }
}
