//! Windows side copy of the game, one per Steam build (plan research/plan_launcher.md sections 3 and 8).
//! base = full copy of Steam's game folder (NTFS has no clone), every file checked against the stock list of the
//! signed index. play = base hard-linked file by file (our own base: Steam never writes it) + the mods; a file a mod
//! changes is always a NEW file put in place by rename, so base stays stock. Steam's folder is only READ.
//!
//! Code mods: a file named in `inject` is a proxy DLL (the multiplayer mod: `baselib.dll`, ds_multiplayer
//! win/NOTES.md section 4). It takes the place of the game file of the same name; the game's own file is kept as
//! `<name>_orig.dll`, which the proxy forwards to. Other files of a code mod go next to the game's exe
//! (`det_ranges_win.tsv`: the DLL reads it next to itself).
//!
//! Plain std file code (no Windows API), so `cargo test` on the Mac runs it too.
use anyhow::{anyhow, bail, Context, Result};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::io::{Read, Write};
use std::path::{Component, Path, PathBuf};

use crate::model::{GameStock, Kind, ModRelease, WIN_X64};
use crate::{now_secs, safe_name, write_atomic, Paths};

/// The game's exe inside a copy (a copy "app" on Windows is the game folder itself).
pub const GAME_EXE: &str = "DungeonSettlers.exe";
/// The multiplayer proxy says this in its version string; the game's own baselib.dll does not.
const PROXY_MARK: &[u8] = b"baselib proxy";
/// Files the friend kit (ds_multiplayer win/friendkit) puts into Steam's folder: never part of a stock copy.
const KIT_FILES: &[&str] = &["baselib_orig.dll", "det_ranges_win.tsv", "DSMP_Play.bat", "DSMP_find_game.bat",
                             "DSMP_Uninstall.bat", "Play_Multiplayer.bat", "steam_appid.txt"];

/// Same fields as the Mac options, so the shell code is the same on both systems. Windows uses none of the
/// Mac-only ones (bundle id, background-only); `extra_stock_dirs` works the same (a stock file by name + sha256).
#[derive(Debug, Clone)]
pub struct CopyOptions {
    pub bundle_id: String,
    pub name: String,
    pub background_only: bool,
    pub extra_stock_dirs: Vec<PathBuf>,
}

impl Default for CopyOptions {
    fn default() -> Self {
        CopyOptions { bundle_id: String::new(), name: "Dungeon Settlers (mods)".into(), background_only: false, extra_stock_dirs: vec![] }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct BaseMarker {
    pub build: String,
    /// sha256 of the stock GameAssembly.dll of this build.
    pub gameassembly_sha256: String,
    /// Number of files checked against the index's stock list (0 = the index had no list for this build).
    #[serde(default)]
    pub checked_files: usize,
    pub created: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct PlayMarker {
    pub build: String,
    /// "id@version" -> sha256 of each file
    pub mods: BTreeMap<String, BTreeMap<String, String>>,
    pub created: u64,
}

pub fn base_app(paths: &Paths, build: &str) -> PathBuf { paths.game_dir(build).join("base") }
pub fn play_dir(paths: &Paths, build: &str, key: &str) -> PathBuf { paths.game_dir(build).join(format!("play-{key}")) }
pub fn play_app(paths: &Paths, build: &str, key: &str) -> PathBuf { play_dir(paths, build, key) }

/// A relative path from the index ("DungeonSettlers_Data/x.bundle", '/' or '\\'): no root, no "..".
fn rel_path(rel: &str) -> Result<PathBuf> {
    let r = PathBuf::from(rel.replace('\\', "/"));
    if rel.is_empty() || r.components().any(|c| !matches!(c, Component::Normal(_))) {
        bail!("path {rel:?} is not inside the game folder");
    }
    Ok(r)
}

fn rel_string(root: &Path, p: &Path) -> Result<String> {
    let r = p.strip_prefix(root)?;
    let parts: Vec<String> = r.components().map(|c| c.as_os_str().to_string_lossy().into_owned()).collect();
    Ok(parts.join("/"))
}

/// Every file under `d` (no folders), sorted.
fn walk(d: &Path) -> Result<Vec<PathBuf>> {
    let mut out = Vec::new();
    let mut stack = vec![d.to_path_buf()];
    while let Some(x) = stack.pop() {
        for e in std::fs::read_dir(&x).with_context(|| format!("read {}", x.display()))? {
            let e = e?;
            let t = e.file_type()?;
            if t.is_dir() { stack.push(e.path()) } else if t.is_file() { out.push(e.path()) }
        }
    }
    out.sort();
    Ok(out)
}

/// Copy one file and return the sha256 of the bytes written (one read of the source).
fn copy_hashing(src: &Path, dst: &Path) -> Result<String> {
    use sha2::{Digest, Sha256};
    if let Some(p) = dst.parent() { std::fs::create_dir_all(p)?; }
    let mut i = std::fs::File::open(src).with_context(|| format!("open {}", src.display()))?;
    let mut o = std::fs::File::create(dst).with_context(|| format!("create {}", dst.display()))?;
    let mut h = Sha256::new();
    let mut buf = vec![0u8; 1 << 20];
    loop {
        let n = i.read(&mut buf)?;
        if n == 0 { break }
        h.update(&buf[..n]);
        o.write_all(&buf[..n])?;
    }
    o.flush()?;
    Ok(hex::encode(h.finalize()))
}

fn is_proxy(p: &Path) -> bool {
    std::fs::read(p).map(|b| b.windows(PROXY_MARK.len()).any(|w| w == PROXY_MARK)).unwrap_or(false)
}

/// Folder that holds our own temporary or replaced copies: deleted, not retired (a Windows copy is ~700 MB and
/// it is the launcher's own unfinished work, never the player's files).
fn discard(p: &Path) -> Result<()> {
    if p.exists() { std::fs::remove_dir_all(p).with_context(|| format!("remove {}", p.display()))?; }
    Ok(())
}

/// The stock source of `rel` in Steam's folder. The friend kit put our proxy at baselib.dll and kept the
/// game's file as baselib_orig.dll: then the stock file is that one.
fn stock_source(steam_game: &Path, rel: &str) -> PathBuf {
    let p = steam_game.join(rel_path(rel).unwrap_or_default());
    if rel.eq_ignore_ascii_case("baselib.dll") && is_proxy(&p) {
        let o = steam_game.join("baselib_orig.dll");
        if o.exists() { return o }
    }
    p
}

/// Stock copy of build `build` from Steam's game folder. Kept as is; play copies are linked from it.
/// With a stock list in the index (`files`: path -> sha256) exactly those files are copied and each one must
/// match. Without one, every file is copied except the friend kit's files, and GameAssembly.dll must match
/// `gameassembly_sha256` when the index has it.
pub fn build_base(paths: &Paths, steam_game: &Path, steam_build: &str, build: &str, stock: &GameStock,
                  opts: &CopyOptions) -> Result<PathBuf> {
    let gd = paths.game_dir(build);
    let base = base_app(paths, build);
    if gd.join("base.json").exists() && base.join(GAME_EXE).exists() { return Ok(base) }
    if steam_build != build {
        bail!("Steam has build {steam_build}, not {build}: a copy of {build} cannot be made from it");
    }
    if !steam_game.join(GAME_EXE).exists() { bail!("no {GAME_EXE} in {}", steam_game.display()) }

    let tmp = gd.join("base.building");
    discard(&tmp)?;
    std::fs::create_dir_all(&tmp)?;
    let mut bad = Vec::new();
    let mut ga_sha = String::new();
    if !stock.files.is_empty() {
        for (rel, want) in &stock.files {
            let r = rel_path(rel)?;
            let src = stock_source(steam_game, rel);
            let dst = tmp.join(&r);
            let got = if src.exists() { copy_hashing(&src, &dst)? } else { String::new() };
            if got.eq_ignore_ascii_case(want) {
                if rel.eq_ignore_ascii_case("GameAssembly.dll") { ga_sha = got; }
                continue;
            }
            let name = r.file_name().unwrap_or_default();
            let fix = opts.extra_stock_dirs.iter().map(|d| d.join(name))
                .find(|s| s.exists() && crate::crypto::sha256_file(s).map(|x| x.eq_ignore_ascii_case(want)).unwrap_or(false));
            match fix {
                Some(s) => { copy_hashing(&s, &dst)?; }
                None => bad.push(rel.clone()),
            }
        }
    } else {
        for f in walk(steam_game)? {
            let rel = rel_string(steam_game, &f)?;
            if KIT_FILES.iter().any(|k| k.eq_ignore_ascii_case(&rel)) || rel.starts_with("dsmp_kit/") { continue }
            let got = copy_hashing(&stock_source(steam_game, &rel), &tmp.join(rel_path(&rel)?))?;
            if rel.eq_ignore_ascii_case("baselib.dll") && is_proxy(&tmp.join("baselib.dll")) {
                bad.push("baselib.dll (the multiplayer file is there, the game's own file is gone)".into());
            }
            if rel.eq_ignore_ascii_case("GameAssembly.dll") {
                if !stock.gameassembly_sha256.is_empty() && !got.eq_ignore_ascii_case(&stock.gameassembly_sha256) {
                    bad.push(rel.clone());
                }
                ga_sha = got;
            }
        }
    }
    if !bad.is_empty() {
        let _ = discard(&tmp);
        bail!("these game files are changed or missing: {}. In Steam, right-click Dungeon Settlers, click \
               Properties, Installed Files, Verify integrity of game files. Then try again.", bad.join(", "));
    }
    if !tmp.join(GAME_EXE).exists() { bail!("the stock list of build {build} has no {GAME_EXE}") }
    discard(&base)?;
    std::fs::rename(&tmp, &base)?;
    let m = BaseMarker { build: build.into(), gameassembly_sha256: ga_sha, checked_files: stock.files.len(), created: now_secs() };
    write_atomic(&gd.join("base.json"), &serde_json::to_vec_pretty(&m)?)?;
    Ok(base)
}

pub fn wanted_marker(build: &str, mods: &[(ModRelease, PathBuf)], _opts: &CopyOptions) -> PlayMarker {
    let mut m = BTreeMap::new();
    for (r, _) in mods {
        let part = r.platforms.get(WIN_X64);
        let mut files: BTreeMap<String, String> = part
            .map(|p| p.files.iter().map(|f| (f.name.clone(), f.sha256.clone())).collect()).unwrap_or_default();
        if let Some(ps) = part.and_then(|p| p.patches.get(build)) {
            files.insert("(patches)".into(), crate::crypto::sha256_hex(&serde_json::to_vec(ps).unwrap_or_default()));
        }
        if let Some(p) = part.filter(|p| !p.inject.is_empty()) { files.insert("(inject)".into(), p.inject.join(",")); }
        m.insert(r.key(), files);
    }
    PlayMarker { build: build.into(), mods: m, created: 0 }
}

/// Short key of a mod set: one play copy per build and mod set.
pub fn set_key(build: &str, mods: &[(ModRelease, PathBuf)], opts: &CopyOptions) -> String {
    let m = wanted_marker(build, mods, opts);
    crate::crypto::sha256_hex(&serde_json::to_vec(&m).unwrap_or_default())[..12].to_string()
}

/// True when the play copy for this mod set exists and is complete.
pub fn play_is_current(paths: &Paths, build: &str, mods: &[(ModRelease, PathBuf)], opts: &CopyOptions) -> bool {
    let key = set_key(build, mods, opts);
    let p = paths.game_dir(build).join(format!("play-{key}.json"));
    let Ok(b) = std::fs::read(p) else { return false };
    let Ok(mut have) = serde_json::from_slice::<PlayMarker>(&b) else { return false };
    have.created = 0;
    play_app(paths, build, &key).join(GAME_EXE).exists() && have == wanted_marker(build, mods, opts)
}

/// Put `src`'s bytes at `dst` as a new file (write next to it, then rename over): a hard link at `dst` is
/// replaced, never written through, so base keeps its stock bytes.
fn place_new(src: &Path, dst: &Path) -> Result<()> {
    if let Some(p) = dst.parent() { std::fs::create_dir_all(p)?; }
    let t = dst.with_extension("modtmp");
    std::fs::copy(src, &t).with_context(|| format!("copy {} -> {}", src.display(), t.display()))?;
    std::fs::rename(&t, dst)?;
    Ok(())
}

/// Apply byte patches (offsets from the file start; Windows has one slice). Each file is read once and
/// written once as a new file. Two mods that change the same bytes are refused. Already patched = fine;
/// neither the old nor the new bytes = refused (wrong build).
pub fn apply_patches(game: &Path, patches: &[(String, crate::model::BytePatch)]) -> Result<()> {
    let mut by_file: BTreeMap<String, Vec<&(String, crate::model::BytePatch)>> = BTreeMap::new();
    for e in patches { by_file.entry(e.1.file.clone()).or_default().push(e); }
    for (file, ps) in by_file {
        let f = game.join(rel_path(&file)?);
        let mut b = std::fs::read(&f).with_context(|| format!("read {}", f.display()))?;
        let mut taken: Vec<(usize, usize, &str)> = Vec::new();
        let mut changed = false;
        for (mod_id, p) in ps {
            if p.slice != "file" { bail!("{mod_id}: slice {:?} is not for Windows (use \"file\")", p.slice) }
            let off = usize::from_str_radix(p.offset.trim_start_matches("0x"), 16)?;
            let expect = hex::decode(&p.expect)?;
            let new = hex::decode(&p.new)?;
            if expect.len() != new.len() || new.is_empty() || off + new.len() > b.len() {
                bail!("{mod_id}: patch {} {} is out of range", p.file, p.offset);
            }
            let (lo, hi) = (off, off + new.len());
            if let Some((_, _, o)) = taken.iter().find(|(a, z, _)| lo < *z && *a < hi) {
                bail!("{o} and {mod_id} both change {} at {}", p.file, p.offset);
            }
            taken.push((lo, hi, mod_id.as_str()));
            let cur = &b[lo..hi];
            if cur == new.as_slice() { continue }
            if cur != expect.as_slice() {
                bail!("{mod_id}: {} at {}: found {}, expected {} (wrong game build?)", p.file, p.offset, hex::encode(cur), p.expect);
            }
            b[lo..hi].copy_from_slice(&new);
            changed = true;
        }
        if changed {
            let t = f.with_extension("patchtmp");
            std::fs::write(&t, &b)?;
            std::fs::rename(&t, &f)?;
        }
    }
    Ok(())
}

/// Play copy = base (hard links, a plain copy where links fail) + mods + steam_appid.txt.
pub fn build_play(paths: &Paths, build: &str, mods: &[(ModRelease, PathBuf)], opts: &CopyOptions) -> Result<PathBuf> {
    let gd = paths.game_dir(build);
    let base = base_app(paths, build);
    if !gd.join("base.json").exists() { bail!("no base copy for build {build}") }
    let key = set_key(build, mods, opts);
    let tmp = gd.join(format!("play-{key}.building"));
    discard(&tmp)?;
    for f in walk(&base)? {
        let dst = tmp.join(f.strip_prefix(&base)?);
        std::fs::create_dir_all(dst.parent().unwrap())?;
        if std::fs::hard_link(&f, &dst).is_err() { std::fs::copy(&f, &dst).with_context(|| format!("copy {}", f.display()))?; }
    }
    let mut placed: BTreeMap<String, String> = BTreeMap::new();
    let mut patches: Vec<(String, crate::model::BytePatch)> = Vec::new();
    let lower = |s: &str| s.to_ascii_lowercase();
    for (m, dir) in mods {
        let part = m.platforms.get(WIN_X64).ok_or_else(|| anyhow!("{} has no Windows files", m.name))?;
        match part.patches.get(build) {
            Some(ps) => patches.extend(ps.iter().map(|p| (m.id.clone(), p.clone()))),
            None if m.kind == Kind::Patch => bail!("{} has no patch for build {build}", m.name),
            None => {}
        }
        for i in &part.inject {
            if !part.files.iter().any(|f| &f.name == i) { bail!("{}: {i} is not one of its files", m.name) }
        }
        for f in &part.files {
            if !safe_name(&f.name) { bail!("bad file name {:?}", f.name) }
            let src = dir.join(&f.name);
            if let Some(dest) = &f.dest {
                // data mod: replace one stock file of the game
                let r = rel_path(dest)?;
                let t = tmp.join(&r);
                if !t.exists() { bail!("{}: {dest} is not in this game build", m.name) }
                if let Some(o) = placed.insert(lower(dest), m.id.clone()) { bail!("{} and {} both replace {dest}", o, m.id) }
                place_new(&src, &t)?;
                continue;
            }
            if let Some(o) = placed.insert(lower(&f.name), m.id.clone()) { bail!("{} and {} both ship {}", o, m.id, f.name) }
            let t = tmp.join(&f.name);
            if part.inject.contains(&f.name) {
                // proxy DLL: the game's own file moves to <name>_orig.dll (the proxy forwards every export to it)
                if t.exists() {
                    let stem = Path::new(&f.name).file_stem().unwrap_or_default().to_string_lossy().into_owned();
                    let orig = format!("{stem}_orig.dll");
                    if let Some(o) = placed.insert(lower(&orig), m.id.clone()) { bail!("{} and {} both need {orig}", o, m.id) }
                    std::fs::rename(&t, tmp.join(&orig))?;
                }
            } else if t.exists() {
                bail!("{}: {} would replace a game file", m.name, f.name);
            }
            place_new(&src, &t)?;
        }
    }
    for (id, p) in &patches {
        if let Some(o) = placed.get(&lower(&p.file)) { bail!("{o} replaces {} and {id} patches it", p.file) }
    }
    apply_patches(&tmp, &patches)?;
    // run from the copy without a restart through Steam (the launcher also sets SteamAppId)
    std::fs::write(tmp.join("steam_appid.txt"), format!("{}\n", crate::steam::APP_ID))?;

    let pd = play_dir(paths, build, &key);
    discard(&pd)?;
    std::fs::rename(&tmp, &pd)?;
    let mut mk = wanted_marker(build, mods, opts);
    mk.created = now_secs();
    write_atomic(&gd.join(format!("play-{key}.json")), &serde_json::to_vec_pretty(&mk)?)?;
    Ok(pd)
}

/// Publisher helper (owner's Mac): the stock list of a Windows game folder, path -> sha256, for the index's
/// `games.<build>.win-x64.files`. Skips the friend kit's files; refuses a folder that holds our proxy.
pub fn stock_list(game: &Path) -> Result<BTreeMap<String, String>> {
    let mut out = BTreeMap::new();
    for f in walk(game)? {
        let rel = rel_string(game, &f)?;
        if KIT_FILES.iter().any(|k| k.eq_ignore_ascii_case(&rel)) || rel.starts_with("dsmp_kit/") { continue }
        if rel.eq_ignore_ascii_case("baselib.dll") && is_proxy(&f) { bail!("{}: baselib.dll is the multiplayer proxy, not stock", game.display()) }
        out.insert(rel, crate::crypto::sha256_file(&f)?);
    }
    if !out.contains_key(GAME_EXE) { bail!("{}: no {GAME_EXE}", game.display()) }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::{BytePatch, FileEntry, PlatformPart};

    fn tdir(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("dsml_win_{name}_{}_{}", std::process::id(), now_secs()));
        let _ = std::fs::remove_dir_all(&d);
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    fn fake_game(d: &Path) {
        std::fs::create_dir_all(d.join("DungeonSettlers_Data/il2cpp_data")).unwrap();
        std::fs::write(d.join(GAME_EXE), b"exe").unwrap();
        std::fs::write(d.join("GameAssembly.dll"), b"ga-stock").unwrap();
        std::fs::write(d.join("baselib.dll"), b"baselib-stock").unwrap();
        std::fs::write(d.join("DungeonSettlers_Data/il2cpp_data/global-metadata.dat"), b"meta0123").unwrap();
    }

    fn entry(dir: &Path, name: &str, body: &[u8]) -> FileEntry {
        std::fs::create_dir_all(dir).unwrap();
        std::fs::write(dir.join(name), body).unwrap();
        FileEntry { name: name.into(), sha256: crate::crypto::sha256_hex(body), size: body.len() as u64,
                    url: format!("https://x.invalid/{name}"), dest: None }
    }

    fn mp_mod(cache: &Path) -> (ModRelease, PathBuf) {
        let mut part = PlatformPart::default();
        part.files = vec![entry(cache, "baselib.dll", b"proxy: baselib proxy v11"), entry(cache, "det_ranges_win.tsv", b"ranges")];
        part.inject = vec!["baselib.dll".into()];
        part.env.insert("DSRIG_DIR".into(), "{profile_dir}".into());
        let m = ModRelease { id: "multiplayer".into(), name: "Multiplayer".into(), version: "0.11.0".into(), author: String::new(),
            kind: Kind::Code, api: 1, scope: Default::default(), difficulty: None, game_builds: vec!["7".into()],
            platforms: [(WIN_X64.to_string(), part)].into(), requires: vec![], conflicts: vec![], description: String::new(), dev_only: false };
        (m, cache.to_path_buf())
    }

    #[test]
    fn base_and_play_keep_steam_and_base_stock() {
        let t = tdir("copy");
        let steam = t.join("steam/Dungeon Settlers");
        fake_game(&steam);
        let paths = Paths::new(t.join("root"));
        let stock = GameStock { gameassembly_sha256: String::new(), files: stock_list(&steam).unwrap() };
        let before: Vec<_> = walk(&steam).unwrap().iter().map(|f| std::fs::read(f).unwrap()).collect();

        assert!(build_base(&paths, &steam, "6", "7", &stock, &Default::default()).unwrap_err().to_string().contains("Steam has build 6"));
        let base = build_base(&paths, &steam, "7", "7", &stock, &Default::default()).unwrap();
        assert_eq!(std::fs::read(base.join("baselib.dll")).unwrap(), b"baselib-stock");

        let mods = vec![mp_mod(&t.join("cache"))];
        assert!(!play_is_current(&paths, "7", &mods, &Default::default()));
        let play = build_play(&paths, "7", &mods, &Default::default()).unwrap();
        assert!(play_is_current(&paths, "7", &mods, &Default::default()));
        assert_eq!(std::fs::read(play.join("baselib.dll")).unwrap(), b"proxy: baselib proxy v11");
        assert_eq!(std::fs::read(play.join("baselib_orig.dll")).unwrap(), b"baselib-stock");
        assert_eq!(std::fs::read(play.join("det_ranges_win.tsv")).unwrap(), b"ranges");
        assert_eq!(std::fs::read_to_string(play.join("steam_appid.txt")).unwrap().trim(), crate::steam::APP_ID);
        assert_eq!(std::fs::read(play.join("DungeonSettlers_Data/il2cpp_data/global-metadata.dat")).unwrap(), b"meta0123");
        // base untouched by the play copy (hard links were replaced, not written through)
        assert_eq!(std::fs::read(base.join("baselib.dll")).unwrap(), b"baselib-stock");
        assert!(!base.join("baselib_orig.dll").exists() && !base.join("det_ranges_win.tsv").exists());
        // Steam's folder untouched
        let after: Vec<_> = walk(&steam).unwrap().iter().map(|f| std::fs::read(f).unwrap()).collect();
        assert_eq!(before, after);
        assert_eq!(paths.copies(), vec!["7".to_string()]);
    }

    #[test]
    fn friend_kit_install_in_steam_folder_still_gives_a_stock_base() {
        let t = tdir("kit");
        let steam = t.join("steam/Dungeon Settlers");
        fake_game(&steam);
        let stock = GameStock { gameassembly_sha256: String::new(), files: stock_list(&steam).unwrap() };
        // what install.bat does
        std::fs::rename(steam.join("baselib.dll"), steam.join("baselib_orig.dll")).unwrap();
        std::fs::write(steam.join("baselib.dll"), b"old kit: baselib proxy v10").unwrap();
        std::fs::write(steam.join("det_ranges_win.tsv"), b"old").unwrap();
        assert!(stock_list(&steam).is_err(), "a kit folder is never published as stock");
        for st in [stock.clone(), GameStock::default()] {
            let paths = Paths::new(t.join(format!("root{}", st.files.len())));
            let base = build_base(&paths, &steam, "7", "7", &st, &Default::default()).unwrap();
            assert_eq!(std::fs::read(base.join("baselib.dll")).unwrap(), b"baselib-stock");
            assert!(!base.join("baselib_orig.dll").exists() && !base.join("det_ranges_win.tsv").exists());
        }
        // kit folder whose original is gone: refused with the Steam "Verify" steps
        std::fs::remove_file(steam.join("baselib_orig.dll")).unwrap();
        let paths = Paths::new(t.join("root_gone"));
        let e = build_base(&paths, &steam, "7", "7", &stock, &Default::default()).unwrap_err().to_string();
        assert!(e.contains("Verify integrity") && e.contains("baselib.dll"), "{e}");
        assert!(paths.copies().is_empty());
    }

    #[test]
    fn changed_game_file_is_refused() {
        let t = tdir("bad");
        let steam = t.join("steam/Dungeon Settlers");
        fake_game(&steam);
        let stock = GameStock { gameassembly_sha256: String::new(), files: stock_list(&steam).unwrap() };
        std::fs::write(steam.join("GameAssembly.dll"), b"ga-modded").unwrap();
        let paths = Paths::new(t.join("root"));
        let e = build_base(&paths, &steam, "7", "7", &stock, &Default::default()).unwrap_err().to_string();
        assert!(e.contains("GameAssembly.dll"), "{e}");
        // a verified stock copy elsewhere fixes it (same rule as the Mac)
        let fix = t.join("stock");
        std::fs::create_dir_all(&fix).unwrap();
        std::fs::write(fix.join("GameAssembly.dll"), b"ga-stock").unwrap();
        let o = CopyOptions { extra_stock_dirs: vec![fix], ..Default::default() };
        let base = build_base(&paths, &steam, "7", "7", &stock, &o).unwrap();
        assert_eq!(std::fs::read(base.join("GameAssembly.dll")).unwrap(), b"ga-stock");
        // no list, but a GameAssembly checksum
        let paths2 = Paths::new(t.join("root2"));
        let only_ga = GameStock { gameassembly_sha256: crate::crypto::sha256_hex(b"ga-stock"), files: Default::default() };
        assert!(build_base(&paths2, &steam, "7", "7", &only_ga, &Default::default()).is_err());
    }

    #[test]
    fn data_and_patch_mods_on_windows() {
        let t = tdir("data");
        let steam = t.join("steam/Dungeon Settlers");
        fake_game(&steam);
        let paths = Paths::new(t.join("root"));
        build_base(&paths, &steam, "7", "7", &GameStock::default(), &Default::default()).unwrap();
        let cache = t.join("cache2");
        let mut data = entry(&cache, "meta.dat", b"META");
        data.dest = Some("DungeonSettlers_Data/il2cpp_data/global-metadata.dat".into());
        let mut part = PlatformPart::default();
        part.files = vec![data];
        part.patches.insert("7".into(), vec![BytePatch { file: "GameAssembly.dll".into(), slice: "file".into(),
            offset: "0x3".into(), expect: hex::encode(b"stock"), new: hex::encode(b"STOCK") }]);
        let (mut m, _) = mp_mod(&t.join("cache"));
        m.id = "hell".into(); m.kind = Kind::Patch; m.platforms = [(WIN_X64.to_string(), part)].into();
        let play = build_play(&paths, "7", &[(m.clone(), cache.clone())], &Default::default()).unwrap();
        assert_eq!(std::fs::read(play.join("GameAssembly.dll")).unwrap(), b"ga-STOCK");
        assert_eq!(std::fs::read(play.join("DungeonSettlers_Data/il2cpp_data/global-metadata.dat")).unwrap(), b"META");
        let base = base_app(&paths, "7");
        assert_eq!(std::fs::read(base.join("GameAssembly.dll")).unwrap(), b"ga-stock");
        assert_eq!(std::fs::read(base.join("DungeonSettlers_Data/il2cpp_data/global-metadata.dat")).unwrap(), b"meta0123");
        // a Mac slice or a path out of the folder is refused
        let mut bad = m.clone();
        bad.platforms.get_mut(WIN_X64).unwrap().patches.get_mut("7").unwrap()[0].slice = "arm64".into();
        assert!(build_play(&paths, "7", &[(bad, cache.clone())], &Default::default()).is_err());
        let mut esc = m;
        esc.platforms.get_mut(WIN_X64).unwrap().files[0].dest = Some("../escape.dat".into());
        assert!(build_play(&paths, "7", &[(esc, cache)], &Default::default()).is_err());
        assert!(!t.join("root/games/escape.dat").exists());
    }
}
