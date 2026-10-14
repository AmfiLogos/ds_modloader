//! Mac side copy of the game, one per Steam build (port of ds_multiplayer/rig/make_mp_app.sh).
//! base = APFS clone of Steam's app with the stock GameAssembly, every resource checked against Steam's
//! own seal. play = clone of base + mods, ad-hoc signed. Steam's install is only READ.
use anyhow::{anyhow, bail, Context, Result};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::process::Command;

use crate::crypto::sha256_file;
use crate::macho;
use crate::model::{GameStock, Kind, ModRelease, MAC_ARM64};
use crate::{now_secs, safe_name, write_atomic, Paths};

pub const GAME_EXE: &str = "Contents/MacOS/Dungeon Settlers";

#[derive(Debug, Clone)]
pub struct CopyOptions {
    pub bundle_id: String,
    pub name: String,
    /// LSBackgroundOnly + LSUIElement (hidden test launches only).
    pub background_only: bool,
    /// Folders with verified stock copies of resource files (the owner's Mac: his live install carries
    /// mod-changed assets). A file is used only when its sha256 equals Steam's seal.
    pub extra_stock_dirs: Vec<PathBuf>,
}

impl Default for CopyOptions {
    fn default() -> Self {
        CopyOptions {
            bundle_id: "com.CanOpener.Dungeon-Settlers.Modded".into(),
            name: "Dungeon Settlers (mods)".into(),
            background_only: false,
            extra_stock_dirs: vec![],
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct BaseMarker {
    pub build: String,
    pub gameassembly_sha256: String,
    pub created: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct PlayMarker {
    pub build: String,
    /// "id@version" -> sha256 of each file
    pub mods: BTreeMap<String, BTreeMap<String, String>>,
    pub bundle_id: String,
    pub background_only: bool,
    pub created: u64,
}

fn run(cmd: &mut Command) -> Result<String> {
    let out = cmd.output().with_context(|| format!("start {:?}", cmd))?;
    if !out.status.success() {
        bail!("{:?} failed: {}", cmd, String::from_utf8_lossy(&out.stderr).trim());
    }
    Ok(String::from_utf8_lossy(&out.stdout).into_owned())
}

/// APFS clone (no extra disk space), recursive.
fn clone(src: &Path, dst: &Path) -> Result<()> {
    run(Command::new("/bin/cp").arg("-c").arg("-R").arg(src).arg(dst)).map(|_| ())
}

pub fn base_app(paths: &Paths, build: &str) -> PathBuf { paths.game_dir(build).join("base/DungeonSettlers.app") }
pub fn play_app(paths: &Paths, build: &str) -> PathBuf { paths.game_dir(build).join("play/DungeonSettlers.app") }

/// Steam seal of the real app: relative path under Contents -> sha256 (hash2) of every sealed resource.
pub fn read_seal(steam_app: &Path) -> Result<BTreeMap<String, Vec<u8>>> {
    let p = steam_app.join("Contents/_CodeSignature/CodeResources");
    let v = plist::Value::from_file(&p).with_context(|| format!("read {}", p.display()))?;
    let files2 = v.as_dictionary().and_then(|d| d.get("files2")).and_then(|f| f.as_dictionary())
        .ok_or_else(|| anyhow!("{}: no files2", p.display()))?;
    let mut out = BTreeMap::new();
    for (rel, e) in files2 {
        if let Some(h) = e.as_dictionary().and_then(|d| d.get("hash2")).and_then(|h| h.as_data()) {
            out.insert(rel.clone(), h.to_vec());
        }
    }
    Ok(out)
}

/// The stock GameAssembly of `build` in Steam's app: the live file, or a `.stock_<build>` file next to it.
pub fn find_stock_gameassembly(steam_app: &Path, build: &str, stock: &GameStock) -> Result<PathBuf> {
    if stock.gameassembly_sha256.is_empty() { bail!("the index has no GameAssembly checksum for build {build}") }
    let fw = steam_app.join("Contents/Frameworks");
    for c in [fw.join("GameAssembly.dylib"), fw.join(format!("GameAssembly.dylib.stock_{build}"))] {
        if c.exists() && sha256_file(&c)?.eq_ignore_ascii_case(&stock.gameassembly_sha256) { return Ok(c) }
    }
    bail!("no stock GameAssembly of build {build} in Steam's install. Use Steam > Properties > \
           Installed Files > Verify integrity of game files, then try again.")
}

/// Stock copy of build `build` from Steam's app. Kept as is; play copies are cloned from it.
pub fn build_base(paths: &Paths, steam_app: &Path, steam_build: &str, build: &str, stock: &GameStock,
                  opts: &CopyOptions) -> Result<PathBuf> {
    let gd = paths.game_dir(build);
    let base = base_app(paths, build);
    if gd.join("base.json").exists() && base.exists() { return Ok(base) }
    if steam_build != build {
        bail!("Steam has build {steam_build}, not {build}: a copy of {build} cannot be made from it");
    }
    let ga = find_stock_gameassembly(steam_app, build, stock)?;
    let seal = read_seal(steam_app)?;

    let tmp = gd.join("base.building");
    if tmp.exists() { paths.retire(&tmp, &format!("{build}-base.building"))?; }
    let c = tmp.join("DungeonSettlers.app/Contents");
    std::fs::create_dir_all(c.join("MacOS"))?;
    std::fs::create_dir_all(c.join("Frameworks"))?;
    let sc = steam_app.join("Contents");
    clone(&sc.join("Resources"), &c.join("Resources"))?;
    if sc.join("PlugIns").exists() { clone(&sc.join("PlugIns"), &c.join("PlugIns"))?; }
    clone(&sc.join("Info.plist"), &c.join("Info.plist"))?;
    clone(&sc.join("MacOS/Dungeon Settlers"), &c.join("MacOS/Dungeon Settlers"))?;
    clone(&sc.join("Frameworks/UnityPlayer.dylib"), &c.join("Frameworks/UnityPlayer.dylib"))?;
    clone(&ga, &c.join("Frameworks/GameAssembly.dylib"))?;

    // files in Resources that Steam's seal does not list are not part of the game (old mod-tool backups)
    let extras = gd.join("not_game_files");
    for f in walk(&c.join("Resources"))? {
        let rel = format!("Resources/{}", f.strip_prefix(c.join("Resources"))?.display());
        if !seal.contains_key(&rel) {
            let dst = extras.join(&rel);
            std::fs::create_dir_all(dst.parent().unwrap())?;
            std::fs::rename(&f, &dst)?;
        }
    }
    // every sealed resource must match; a mod-changed file is put back from a verified stock copy
    let mut bad = Vec::new();
    for (rel, h) in &seal {
        if !rel.starts_with("Resources/") { continue; }
        let p = c.join(rel);
        if p.exists() && hex::decode(sha256_file(&p)?)? == *h { continue; }
        let name = Path::new(rel).file_name().unwrap();
        let fix = opts.extra_stock_dirs.iter().map(|d| d.join(name))
            .find(|s| s.exists() && sha256_file(s).ok().and_then(|x| hex::decode(x).ok()).as_deref() == Some(h.as_slice()));
        match fix {
            Some(s) => {
                let t = p.with_extension("stocktmp");
                std::fs::copy(&s, &t)?;
                std::fs::rename(&t, &p)?;
            }
            None => bad.push(rel.clone()),
        }
    }
    if !bad.is_empty() {
        bail!("these game files are changed and no stock copy was found: {}. Use Steam 'Verify integrity \
               of game files', then try again.", bad.join(", "));
    }
    if gd.join("base").exists() { paths.retire(&gd.join("base"), &format!("{build}-base-unfinished"))?; }
    std::fs::rename(&tmp, gd.join("base"))?;
    let m = BaseMarker { build: build.into(), gameassembly_sha256: stock.gameassembly_sha256.clone(), created: now_secs() };
    write_atomic(&gd.join("base.json"), &serde_json::to_vec_pretty(&m)?)?;
    Ok(base)
}

fn walk(d: &Path) -> Result<Vec<PathBuf>> {
    let mut out = Vec::new();
    let mut stack = vec![d.to_path_buf()];
    while let Some(x) = stack.pop() {
        for e in std::fs::read_dir(&x)? {
            let e = e?;
            let t = e.file_type()?;
            if t.is_dir() { stack.push(e.path()) } else { out.push(e.path()) }
        }
    }
    Ok(out)
}

pub fn wanted_marker(build: &str, mods: &[(ModRelease, PathBuf)], opts: &CopyOptions) -> PlayMarker {
    let mut m = BTreeMap::new();
    for (r, _) in mods {
        let files = r.platforms.get(MAC_ARM64).map(|p| p.files.iter().map(|f| (f.name.clone(), f.sha256.clone())).collect())
            .unwrap_or_default();
        m.insert(r.key(), files);
    }
    PlayMarker { build: build.into(), mods: m, bundle_id: opts.bundle_id.clone(), background_only: opts.background_only, created: 0 }
}

/// True when the play copy already has exactly these mods.
pub fn play_is_current(paths: &Paths, build: &str, mods: &[(ModRelease, PathBuf)], opts: &CopyOptions) -> bool {
    let p = paths.game_dir(build).join("play.json");
    let Ok(b) = std::fs::read(p) else { return false };
    let Ok(mut have) = serde_json::from_slice::<PlayMarker>(&b) else { return false };
    have.created = 0;
    play_app(paths, build).exists() && have == wanted_marker(build, mods, opts)
}

/// Apply one byte patch inside the app. Already patched = fine. Neither old nor new bytes = refused.
fn apply_patch(app: &Path, p: &crate::model::BytePatch) -> Result<PathBuf> {
    let rel = Path::new(&p.file);
    if rel.is_absolute() || rel.components().any(|c| matches!(c, std::path::Component::ParentDir)) {
        bail!("patch path {:?} is not inside the app", p.file);
    }
    let f = app.join(rel);
    let mut b = std::fs::read(&f).with_context(|| format!("read {}", f.display()))?;
    let (base, size) = if p.slice == "arm64" { macho::arm64_slice(&b)? } else { (0, b.len()) };
    let off = usize::from_str_radix(p.offset.trim_start_matches("0x"), 16)?;
    let expect = hex::decode(&p.expect)?;
    let new = hex::decode(&p.new)?;
    if expect.len() != new.len() || off + new.len() > size { bail!("patch {} {} is out of range", p.file, p.offset) }
    let at = base + off;
    let cur = &b[at..at + new.len()];
    if cur == new.as_slice() { return Ok(f) }
    if cur != expect.as_slice() {
        bail!("{} at {}: found {}, expected {} (wrong game build?)", p.file, p.offset, hex::encode(cur), p.expect);
    }
    b[at..at + new.len()].copy_from_slice(&new);
    // new inode (never write into a file a process may have mapped)
    let t = f.with_extension("patchtmp");
    std::fs::write(&t, &b)?;
    std::fs::rename(&t, &f)?;
    Ok(f)
}

/// Play copy = clone of base + mods, then one ad-hoc signature.
pub fn build_play(paths: &Paths, build: &str, mods: &[(ModRelease, PathBuf)], opts: &CopyOptions) -> Result<PathBuf> {
    let gd = paths.game_dir(build);
    let base = base_app(paths, build);
    if !gd.join("base.json").exists() { bail!("no base copy for build {build}") }
    let tmp = gd.join("play.building");
    if tmp.exists() { paths.retire(&tmp, &format!("{build}-play.building"))?; }
    std::fs::create_dir_all(&tmp)?;
    let app = tmp.join("DungeonSettlers.app");
    clone(&base, &app)?;
    let c = app.join("Contents");
    let fw = c.join("Frameworks");
    let os = macho::running_macos();
    let mut to_sign: Vec<PathBuf> = Vec::new();
    let mut placed: BTreeMap<String, String> = BTreeMap::new();

    for (m, dir) in mods {
        let part = m.platforms.get(MAC_ARM64).ok_or_else(|| anyhow!("{} has no Mac files", m.name))?;
        if let Some(ps) = part.patches.get(build) {
            for p in ps { let f = apply_patch(&app, p)?; if !to_sign.contains(&f) { to_sign.push(f) } }
        } else if m.kind == Kind::Patch {
            bail!("{} has no patch for build {build}", m.name);
        }
        for f in &part.files {
            if !safe_name(&f.name) { bail!("bad file name {:?}", f.name) }
            let src = dir.join(&f.name);
            let bytes = std::fs::read(&src)?;
            if let Some(dest) = &f.dest {
                // data mod: replace one stock file of the game
                let rel = Path::new(dest);
                if rel.is_absolute() || rel.components().any(|c| matches!(c, std::path::Component::ParentDir)) || !dest.starts_with("Contents/Resources/") {
                    bail!("{}: target {dest:?} is not a game resource", m.name);
                }
                let t = app.join(rel);
                if !t.exists() { bail!("{}: {dest} is not in this game build", m.name) }
                if let Some(o) = placed.insert(dest.clone(), m.id.clone()) { bail!("{} and {} both replace {dest}", o, m.id) }
                std::fs::copy(&src, t.with_extension("modtmp"))?;
                std::fs::rename(t.with_extension("modtmp"), &t)?;
                continue;
            }
            if let Some(o) = placed.insert(f.name.clone(), m.id.clone()) { bail!("{} and {} both ship {}", o, m.id, f.name) }
            if fw.join(&f.name).exists() { bail!("{}: {} would replace a game file", m.name, f.name) }
            if macho::is_macho(&bytes) {
                let (maj, min) = macho::min_macos(&bytes)?;
                if (maj, min) > os {
                    bail!("{}: {} needs macOS {maj}.{min}, this Mac has {}.{}", m.name, f.name, os.0, os.1);
                }
                std::fs::copy(&src, fw.join(&f.name))?;
                // keep the published bytes when the file is already validly signed (make_mp_app.sh rule)
                let signed = Command::new("/usr/bin/codesign").arg("-v").arg(fw.join(&f.name)).output()?.status.success();
                if !signed { to_sign.push(fw.join(&f.name)); }
            } else {
                // data file of a code mod: lives in Resources (a plain file in Frameworks breaks codesign),
                // a link next to the dylib (the multiplayer mod reads det_ranges.tsv next to itself)
                let rd = c.join("Resources/mods").join(&m.id);
                std::fs::create_dir_all(&rd)?;
                std::fs::copy(&src, rd.join(&f.name))?;
                std::os::unix::fs::symlink(format!("../Resources/mods/{}/{}", m.id, f.name), fw.join(&f.name))?;
            }
        }
    }

    let pl = c.join("Info.plist");
    run(Command::new("/usr/bin/plutil").args(["-replace", "CFBundleIdentifier", "-string", &opts.bundle_id]).arg(&pl))?;
    run(Command::new("/usr/bin/plutil").args(["-replace", "CFBundleName", "-string", &opts.name]).arg(&pl))?;
    if opts.background_only {
        for k in ["LSBackgroundOnly", "LSUIElement"] {
            run(Command::new("/usr/bin/plutil").args(["-replace", k, "-bool", "YES"]).arg(&pl))?;
        }
    }
    // downloaded files must not carry quarantine; then ad-hoc signatures inside-out (the game binary loses
    // Steam's hardened runtime so DYLD_INSERT_LIBRARIES can load the mods)
    run(Command::new("/usr/bin/xattr").arg("-cr").arg(&app))?;
    for f in &to_sign { run(Command::new("/usr/bin/codesign").args(["-f", "-s", "-"]).arg(f))?; }
    run(Command::new("/usr/bin/codesign").args(["-f", "-s", "-"]).arg(c.join("MacOS/Dungeon Settlers")))?;
    run(Command::new("/usr/bin/nice").args(["-n", "10", "/usr/bin/codesign", "-f", "-s", "-"]).arg(&app))?;
    run(Command::new("/usr/bin/codesign").arg("-v").arg(&app)).context("signature check of the new copy")?;

    let play_dir = gd.join("play");
    if play_dir.exists() { paths.retire(&play_dir, &format!("{build}-play"))?; }
    std::fs::rename(&tmp, &play_dir)?;
    let mut mk = wanted_marker(build, mods, opts);
    mk.created = now_secs();
    write_atomic(&gd.join("play.json"), &serde_json::to_vec_pretty(&mk)?)?;
    Ok(play_app(paths, build))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::BytePatch;

    #[test]
    fn byte_patch_checks_old_bytes() {
        let d = std::env::temp_dir().join(format!("dsml_patch_{}", std::process::id()));
        std::fs::create_dir_all(d.join("Contents")).unwrap();
        // thin arm64 header + payload
        let mut b = vec![0u8; 64];
        b[0..4].copy_from_slice(&0xFEEDFACFu32.to_le_bytes());
        b[4..8].copy_from_slice(&0x0100000Cu32.to_le_bytes());
        b[40..44].copy_from_slice(&[0x20, 0x00, 0x80, 0x52]);
        std::fs::write(d.join("Contents/x.dylib"), &b).unwrap();
        let mut p = BytePatch { file: "Contents/x.dylib".into(), slice: "arm64".into(), offset: "0x28".into(),
                                expect: "20008052".into(), new: "00008052".into() };
        apply_patch(&d, &p).unwrap();
        assert_eq!(&std::fs::read(d.join("Contents/x.dylib")).unwrap()[40..44], &[0, 0, 0x80, 0x52]);
        apply_patch(&d, &p).unwrap(); // already patched: fine
        p.expect = "11111111".into(); p.new = "22222222".into();
        assert!(apply_patch(&d, &p).unwrap_err().to_string().contains("wrong game build"));
        p.file = "../escape".into();
        assert!(apply_patch(&d, &p).is_err());
    }
}
