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
    /// COPY_IDENT of the copy (0 = made before own_identity)
    #[serde(default)]
    pub ident: u32,
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
        let mut files: BTreeMap<String, String> = r.platforms.get(MAC_ARM64)
            .map(|p| p.files.iter().map(|f| (f.name.clone(), f.sha256.clone())).collect()).unwrap_or_default();
        if let Some(ps) = r.platforms.get(MAC_ARM64).and_then(|p| p.patches.get(build)) {
            files.insert("(patches)".into(), crate::crypto::sha256_hex(&serde_json::to_vec(ps).unwrap_or_default()));
        }
        m.insert(r.key(), files);
    }
    PlayMarker { build: build.into(), mods: m, bundle_id: opts.bundle_id.clone(), background_only: opts.background_only, ident: COPY_IDENT, created: 0 }
}

/// Short key of a mod set: one play copy per build and mod set (worlds with other mods get their own copy).
pub fn set_key(build: &str, mods: &[(ModRelease, PathBuf)], opts: &CopyOptions) -> String {
    let m = wanted_marker(build, mods, opts);
    crate::crypto::sha256_hex(&serde_json::to_vec(&m).unwrap_or_default())[..12].to_string()
}

pub fn play_dir(paths: &Paths, build: &str, key: &str) -> PathBuf { paths.game_dir(build).join(format!("play-{key}")) }
pub fn play_app(paths: &Paths, build: &str, key: &str) -> PathBuf { play_dir(paths, build, key).join("DungeonSettlers.app") }

/// True when the play copy for this mod set exists and is complete.
pub fn play_is_current(paths: &Paths, build: &str, mods: &[(ModRelease, PathBuf)], opts: &CopyOptions) -> bool {
    let key = set_key(build, mods, opts);
    let p = paths.game_dir(build).join(format!("play-{key}.json"));
    let Ok(b) = std::fs::read(p) else { return false };
    let Ok(mut have) = serde_json::from_slice::<PlayMarker>(&b) else { return false };
    have.created = 0;
    play_app(paths, build, &key).exists() && have == wanted_marker(build, mods, opts)
}

fn inside_app(rel: &str) -> Result<&Path> {
    let r = Path::new(rel);
    if r.is_absolute() || r.components().any(|c| matches!(c, std::path::Component::ParentDir)) || !rel.starts_with("Contents/") {
        bail!("path {rel:?} is not inside the app");
    }
    Ok(r)
}

/// Apply byte patches (several mods, several files). Each file is read and written once. Two mods that patch
/// the same bytes are refused. Already patched = fine; neither old nor new bytes = refused (wrong build).
/// Returns the patched Mach-O files (they need a new signature).
pub fn apply_patches(app: &Path, patches: &[(String, crate::model::BytePatch)]) -> Result<Vec<PathBuf>> {
    let mut by_file: BTreeMap<String, Vec<&(String, crate::model::BytePatch)>> = BTreeMap::new();
    for e in patches { by_file.entry(e.1.file.clone()).or_default().push(e); }
    let mut out = Vec::new();
    for (file, ps) in by_file {
        let f = app.join(inside_app(&file)?);
        let mut b = std::fs::read(&f).with_context(|| format!("read {}", f.display()))?;
        let mut taken: Vec<(usize, usize, &str)> = Vec::new();
        let mut changed = false;
        for (mod_id, p) in ps {
            let (base, size) = match p.slice.as_str() {
                "arm64" => macho::arm64_slice(&b)?,
                "file" => (0, b.len()),
                s => bail!("{mod_id}: unknown slice {s:?}"),
            };
            let off = usize::from_str_radix(p.offset.trim_start_matches("0x"), 16)?;
            let expect = hex::decode(&p.expect)?;
            let new = hex::decode(&p.new)?;
            if expect.len() != new.len() || new.is_empty() || off + new.len() > size {
                bail!("{mod_id}: patch {} {} is out of range", p.file, p.offset);
            }
            let (lo, hi) = (base + off, base + off + new.len());
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
            // new inode (never write into a file a process may have mapped)
            let t = f.with_extension("patchtmp");
            std::fs::write(&t, &b)?;
            std::fs::rename(&t, &f)?;
        }
        if macho::is_macho(&b) { out.push(f); }
    }
    Ok(out)
}

/// Version of `own_identity`; in the play markers so copies made before it are made again.
pub const COPY_IDENT: u32 = 1;

/// The text macOS shows in its Local Network alert, per InfoPlist.strings folder (the game's 10 languages).
/// The text macOS shows in its Local Network alert: "<lproj>\t<text>" lines, the game's 10 languages, English first
/// (also read by app/build_app.sh).
const LAN_USAGE: &str = include_str!("lan_usage.tsv");
fn lan_usage() -> impl Iterator<Item = (&'static str, &'static str)> { LAN_USAGE.lines().filter_map(|l| l.split_once('\t')) }

/// The copy's own identity for macOS Local Network privacy (Apple TN3179): the main executable gets a UUID of its
/// own (Steam's UUID makes macOS check the Steam game's answer and never ask, while it blocks the copy), the same
/// for every rebuild of one bundle id so the player's answer is kept; plus the alert's text in 10 languages.
/// Before the signatures (it changes the game binary). Returns true when something was changed.
pub fn own_identity(app: &Path, bundle_id: &str) -> Result<bool> {
    let c = app.join("Contents");
    let exe = app.join(GAME_EXE);
    let mut b = std::fs::read(&exe).with_context(|| format!("read {}", exe.display()))?;
    let mut changed = false;
    for (i, o) in macho::uuid_offsets(&b)?.into_iter().enumerate() {
        let new = copy_uuid(bundle_id, i);
        if b[o..o + 16] == new { continue }
        b[o..o + 16].copy_from_slice(&new);
        changed = true;
    }
    if changed {
        let t = exe.with_extension("uuidtmp");
        std::fs::write(&t, &b)?;
        std::fs::set_permissions(&t, std::fs::metadata(&exe)?.permissions())?;
        std::fs::rename(&t, &exe)?;
    }
    let pl = c.join("Info.plist");
    let en = lan_usage().next().map(|x| x.1).unwrap_or_default();
    let has = plist::Value::from_file(&pl).ok().and_then(|v| v.as_dictionary()
        .and_then(|d| d.get("NSLocalNetworkUsageDescription")).and_then(|s| s.as_string()).map(|s| s == en)).unwrap_or(false);
    if !has {
        run(Command::new("/usr/bin/plutil").args(["-replace", "NSLocalNetworkUsageDescription", "-string", en]).arg(&pl))?;
        changed = true;
    }
    for (lang, text) in lan_usage() {
        let d = c.join(format!("Resources/{lang}.lproj"));
        let want = format!("\"NSLocalNetworkUsageDescription\" = \"{text}\";\n");
        if std::fs::read_to_string(d.join("InfoPlist.strings")).ok().as_deref() == Some(want.as_str()) { continue }
        std::fs::create_dir_all(&d)?;
        std::fs::write(d.join("InfoPlist.strings"), want)?;
        changed = true;
    }
    Ok(changed)
}

/// UUID of slice `i` of a copy: from the bundle id only (no two bundle ids share one; game updates keep it).
fn copy_uuid(bundle_id: &str, i: usize) -> [u8; 16] {
    use sha2::{Digest, Sha256};
    let h = Sha256::digest(format!("dsml copy uuid {i} {bundle_id}").as_bytes());
    let mut u = [0u8; 16];
    u.copy_from_slice(&h[..16]);
    u[6] = (u[6] & 0x0f) | 0x50;   // name-based UUID (version 5 layout)
    u[8] = (u[8] & 0x3f) | 0x80;
    u
}

/// The play copy's own start file (app/stub/ds_play_start.c), next to the game binary in Contents/MacOS.
pub const STARTER: &str = "dsml-start";

/// Makes `starter` the copy's CFBundleExecutable: what macOS runs when it starts the copy as an app (the Dock icon kept
/// from the running game, Spotlight, the loader's soft restart). A direct start then hands over to the app instead of
/// running the game without mods (owner 2026-10-24); the app's stub still execs the game binary itself. The starter
/// gets a UUID of its own per bundle id (as `own_identity`: no program shares one). Before the signatures.
pub fn install_starter(app: &Path, starter: &[u8], bundle_id: &str) -> Result<()> {
    let mut b = starter.to_vec();
    for (i, o) in macho::uuid_offsets(&b)?.into_iter().enumerate() {
        b[o..o + 16].copy_from_slice(&copy_uuid(bundle_id, 64 + i));
    }
    let dst = app.join("Contents/MacOS").join(STARTER);
    let t = dst.with_extension("tmp");
    std::fs::write(&t, &b)?;
    use std::os::unix::fs::PermissionsExt;
    std::fs::set_permissions(&t, std::fs::Permissions::from_mode(0o755))?;
    std::fs::rename(&t, &dst)?;
    run(Command::new("/usr/bin/plutil").args(["-replace", "CFBundleExecutable", "-string", STARTER])
        .arg(app.join("Contents/Info.plist")))?;
    Ok(())
}

/// Publisher helper: the byte patches that turn `stock` into `modded` (same size). Runs closer than 16 bytes
/// are merged. `slice` "arm64" compares the arm64 slices only.
pub fn diff_patches(stock: &[u8], modded: &[u8], file: &str, slice: &str) -> Result<Vec<crate::model::BytePatch>> {
    let (a, b) = if slice == "arm64" {
        let (o1, s1) = macho::arm64_slice(stock)?;
        let (o2, s2) = macho::arm64_slice(modded)?;
        if s1 != s2 { bail!("arm64 slices differ in size: not a byte patch") }
        (&stock[o1..o1 + s1], &modded[o2..o2 + s2])
    } else {
        if stock.len() != modded.len() { bail!("files differ in size: not a byte patch") }
        (stock, modded)
    };
    let mut out = Vec::new();
    let mut i = 0;
    while i < a.len() {
        if a[i] == b[i] { i += 1; continue }
        let start = i;
        let mut end = i;
        while i < a.len() && i < end + 16 { if a[i] != b[i] { end = i + 1 } i += 1 }
        out.push(crate::model::BytePatch {
            file: file.into(), slice: slice.into(), offset: format!("0x{start:X}"),
            expect: hex::encode(&a[start..end]), new: hex::encode(&b[start..end]),
        });
        i = end;
    }
    Ok(out)
}

/// Play copy = clone of base + mods, then one ad-hoc signature.
pub fn build_play(paths: &Paths, build: &str, mods: &[(ModRelease, PathBuf)], opts: &CopyOptions) -> Result<PathBuf> {
    let gd = paths.game_dir(build);
    let base = base_app(paths, build);
    if !gd.join("base.json").exists() { bail!("no base copy for build {build}") }
    let key = set_key(build, mods, opts);
    let tmp = gd.join(format!("play-{key}.building"));
    if tmp.exists() { paths.retire(&tmp, &format!("{build}-play-{key}.building"))?; }
    std::fs::create_dir_all(&tmp)?;
    let app = tmp.join("DungeonSettlers.app");
    clone(&base, &app)?;
    let c = app.join("Contents");
    let fw = c.join("Frameworks");
    let os = macho::running_macos();
    let mut to_sign: Vec<PathBuf> = Vec::new();
    let mut placed: BTreeMap<String, String> = BTreeMap::new();
    let mut patches: Vec<(String, crate::model::BytePatch)> = Vec::new();

    for (m, dir) in mods {
        let part = m.platforms.get(MAC_ARM64).ok_or_else(|| anyhow!("{} has no Mac files", m.name))?;
        match part.patches.get(build) {
            Some(ps) => patches.extend(ps.iter().map(|p| (m.id.clone(), p.clone()))),
            None if m.kind == Kind::Patch => bail!("{} has no patch for build {build}", m.name),
            None => {}
        }
        for f in &part.files {
            if !safe_name(&f.name) { bail!("bad file name {:?}", f.name) }
            let src = dir.join(&f.name);
            let bytes = std::fs::read(&src)?;
            if let Some(dest) = &f.dest {
                // data mod: replace one stock file of the game
                let rel = inside_app(dest)?;
                if !dest.starts_with("Contents/Resources/") { bail!("{}: target {dest:?} is not a game resource", m.name) }
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
    // byte patches after the file replacements; a patch on a file another mod replaced is a conflict
    for (id, p) in &patches {
        if let Some(o) = placed.get(&p.file) { bail!("{o} replaces {} and {id} patches it", p.file) }
    }
    for f in apply_patches(&app, &patches)? { if !to_sign.contains(&f) { to_sign.push(f) } }

    let pl = c.join("Info.plist");
    run(Command::new("/usr/bin/plutil").args(["-replace", "CFBundleIdentifier", "-string", &opts.bundle_id]).arg(&pl))?;
    run(Command::new("/usr/bin/plutil").args(["-replace", "CFBundleName", "-string", &opts.name]).arg(&pl))?;
    if opts.background_only {
        for k in ["LSBackgroundOnly", "LSUIElement"] {
            run(Command::new("/usr/bin/plutil").args(["-replace", k, "-bool", "YES"]).arg(&pl))?;
        }
    }
    own_identity(&app, &opts.bundle_id)?;
    // downloaded files must not carry quarantine; then ad-hoc signatures inside-out (the game binary loses
    // Steam's hardened runtime so DYLD_INSERT_LIBRARIES can load the mods)
    run(Command::new("/usr/bin/xattr").arg("-cr").arg(&app))?;
    for f in &to_sign { run(Command::new("/usr/bin/codesign").args(["-f", "-s", "-"]).arg(f))?; }
    run(Command::new("/usr/bin/codesign").args(["-f", "-s", "-"]).arg(c.join("MacOS/Dungeon Settlers")))?;
    run(Command::new("/usr/bin/nice").args(["-n", "10", "/usr/bin/codesign", "-f", "-s", "-"]).arg(&app))?;
    run(Command::new("/usr/bin/codesign").arg("-v").arg(&app)).context("signature check of the new copy")?;

    let pd = play_dir(paths, build, &key);
    if pd.exists() { paths.retire(&pd, &format!("{build}-play-{key}"))?; }
    std::fs::rename(&tmp, &pd)?;
    let mut mk = wanted_marker(build, mods, opts);
    mk.created = now_secs();
    write_atomic(&gd.join(format!("play-{key}.json")), &serde_json::to_vec_pretty(&mk)?)?;
    Ok(play_app(paths, build, &key))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::BytePatch;

    fn thin_dylib(d: &Path) {
        std::fs::create_dir_all(d.join("Contents")).unwrap();
        let mut b = vec![0u8; 64];
        b[0..4].copy_from_slice(&0xFEEDFACFu32.to_le_bytes());
        b[4..8].copy_from_slice(&0x0100000Cu32.to_le_bytes());
        b[40..44].copy_from_slice(&[0x20, 0x00, 0x80, 0x52]);
        std::fs::write(d.join("Contents/x.dylib"), &b).unwrap();
        std::fs::write(d.join("Contents/catalog.bin"), b"0123456789abcdef").unwrap();
    }

    fn bp(file: &str, slice: &str, off: &str, expect: &str, new: &str) -> BytePatch {
        BytePatch { file: file.into(), slice: slice.into(), offset: off.into(), expect: expect.into(), new: new.into() }
    }

    /// Steam's UUID left in a copy = macOS blocks it on the local network and never asks (TN3179).
    #[test]
    fn own_identity_gives_each_bundle_id_its_own_stable_uuid() {
        let fat = |u: [u8; 16]| {
            let mut sl = vec![0u8; 32 + 24];
            sl[0..4].copy_from_slice(&0xFEEDFACFu32.to_le_bytes());
            sl[16..20].copy_from_slice(&1u32.to_le_bytes());
            sl[32..36].copy_from_slice(&0x1Bu32.to_le_bytes());
            sl[36..40].copy_from_slice(&24u32.to_le_bytes());
            sl[40..56].copy_from_slice(&u);
            let mut f = vec![0u8; 64];
            f[0..4].copy_from_slice(&0xCAFEBABEu32.to_be_bytes());
            f[4..8].copy_from_slice(&2u32.to_be_bytes());
            for (i, off) in [64u32, 64 + sl.len() as u32].iter().enumerate() {
                f[8 + 20 * i + 8..8 + 20 * i + 12].copy_from_slice(&off.to_be_bytes());
                f[8 + 20 * i + 12..8 + 20 * i + 16].copy_from_slice(&(sl.len() as u32).to_be_bytes());
            }
            f.extend_from_slice(&sl);
            f.extend_from_slice(&sl);
            f
        };
        let steam = fat([7; 16]);
        let mut uuids = Vec::new();
        for id in ["com.CanOpener.Dungeon-Settlers.Modded", "com.CanOpener.Dungeon-Settlers.mpA"] {
            let d = std::env::temp_dir().join(format!("dsml_ident_{}_{id}", std::process::id())).join("X.app");
            std::fs::create_dir_all(d.join("Contents/MacOS")).unwrap();
            std::fs::create_dir_all(d.join("Contents/Resources")).unwrap();
            std::fs::write(d.join(GAME_EXE), &steam).unwrap();
            plist::Value::Dictionary(plist::Dictionary::new()).to_file_xml(d.join("Contents/Info.plist")).unwrap();
            assert!(own_identity(&d, id).unwrap());
            let b = std::fs::read(d.join(GAME_EXE)).unwrap();
            let u: Vec<Vec<u8>> = macho::uuid_offsets(&b).unwrap().iter().map(|&o| b[o..o + 16].to_vec()).collect();
            assert_eq!(u.len(), 2);
            assert!(u.iter().all(|x| x != &[7u8; 16]) && u[0] != u[1], "every slice gets a new UUID");
            assert!(!own_identity(&d, id).unwrap(), "a second run (rebuild, pinned copy) keeps the same identity");
            let fr = std::fs::read_to_string(d.join("Contents/Resources/fr.lproj/InfoPlist.strings")).unwrap();
            assert!(fr.starts_with("\"NSLocalNetworkUsageDescription\" = \"Le multijoueur"));
            uuids.push(u);
        }
        assert_ne!(uuids[0], uuids[1], "two copies never share a UUID");
    }

    #[test]
    fn byte_patch_checks_old_bytes() {
        let d = std::env::temp_dir().join(format!("dsml_patch_{}", std::process::id()));
        thin_dylib(&d);
        let p = bp("Contents/x.dylib", "arm64", "0x28", "20008052", "00008052");
        let signed = apply_patches(&d, &[("m".into(), p.clone())]).unwrap();
        assert_eq!(signed, vec![d.join("Contents/x.dylib")]);
        assert_eq!(&std::fs::read(d.join("Contents/x.dylib")).unwrap()[40..44], &[0, 0, 0x80, 0x52]);
        apply_patches(&d, &[("m".into(), p)]).unwrap(); // already patched: fine
        let bad = bp("Contents/x.dylib", "arm64", "0x28", "11111111", "22222222");
        assert!(apply_patches(&d, &[("m".into(), bad)]).unwrap_err().to_string().contains("wrong game build"));
        let esc = bp("../escape", "file", "0x0", "00", "01");
        assert!(apply_patches(&d, &[("m".into(), esc)]).is_err());
    }

    #[test]
    fn two_mods_compose_on_one_file_but_not_on_the_same_bytes() {
        let d = std::env::temp_dir().join(format!("dsml_patch2_{}", std::process::id()));
        thin_dylib(&d);
        // like Hell (catalog offset 51234) + weapon slot (51768): different bytes of catalog.bin
        let a = bp("Contents/catalog.bin", "file", "0x2", "3233", "5858"); // "23" -> "XX"
        let b = bp("Contents/catalog.bin", "file", "0xA", "6162", "5959"); // "ab" -> "YY"
        let out = apply_patches(&d, &[("hell".into(), a.clone()), ("ws".into(), b)]).unwrap();
        assert!(out.is_empty(), "catalog.bin is not Mach-O: no signature");
        assert_eq!(std::fs::read(d.join("Contents/catalog.bin")).unwrap(), b"01XX456789YYcdef");
        let c = bp("Contents/catalog.bin", "file", "0x3", "5834", "0000");
        let err = apply_patches(&d, &[("hell".into(), a), ("other".into(), c)]).unwrap_err().to_string();
        assert!(err.contains("both change"), "{err}");
    }

    #[test]
    fn diff_makes_patches_that_apply() {
        let stock = b"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa".to_vec();
        let mut modded = stock.clone();
        modded[3] = b'X'; modded[6] = b'Y'; modded[40] = b'Z';
        let ps = diff_patches(&stock, &modded, "Contents/f.bin", "file").unwrap();
        assert_eq!(ps.len(), 2, "near runs merge, far runs split: {ps:?}");
        assert_eq!(ps[0].offset, "0x3");
        let d = std::env::temp_dir().join(format!("dsml_diff_{}", std::process::id()));
        std::fs::create_dir_all(d.join("Contents")).unwrap();
        std::fs::write(d.join("Contents/f.bin"), &stock).unwrap();
        let tagged: Vec<_> = ps.into_iter().map(|p| ("m".to_string(), p)).collect();
        apply_patches(&d, &tagged).unwrap();
        assert_eq!(std::fs::read(d.join("Contents/f.bin")).unwrap(), modded);
    }
}
