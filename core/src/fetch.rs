//! Download the signed index and mod files (GitHub Releases in production; plain https GETs, never the
//! REST API). Nothing is used before its signature / sha256 is checked.
use anyhow::{bail, Context, Result};
use std::io::Read;
use std::path::PathBuf;

use crate::crypto::{check_file, check_seq, verify_index};
use crate::model::{Index, ModRelease, State};
use crate::{safe_name, write_atomic, Paths};

/// Production index (placeholder until the owner picks the repo name). DSML_INDEX_URL overrides.
pub const DEFAULT_INDEX_URL: &str = "https://github.com/OWNER/dungeon-settlers-mods/releases/download/index/index.json";

pub fn index_url() -> String {
    std::env::var("DSML_INDEX_URL").unwrap_or_else(|_| DEFAULT_INDEX_URL.to_string())
}

const MAX_FILE: u64 = 512 << 20;

pub fn http_get(url: &str) -> Result<Vec<u8>> {
    if !(url.starts_with("https://") || url.starts_with("http://127.0.0.1") || url.starts_with("http://localhost")) {
        bail!("refused url {url}: https only (plain http only for a local test server)");
    }
    let resp = ureq::get(url).call().with_context(|| format!("download {url}"))?;
    let mut buf = Vec::new();
    resp.into_body().into_reader().take(MAX_FILE).read_to_end(&mut buf)?;
    Ok(buf)
}

/// Download index.json + index.json.sig, check the signature and the replay guard, keep both on disk.
pub fn fetch_index(paths: &Paths, url: &str, trusted: &[&str], state: &mut State) -> Result<Index> {
    let data = http_get(url)?;
    let sig = http_get(&format!("{url}.sig"))?;
    let idx = verify_index(&data, &sig, trusted)?;
    check_seq(&idx, state.last_index_seq)?;
    std::fs::create_dir_all(&paths.root)?;
    write_atomic(&paths.root.join("index.json"), &data)?;
    write_atomic(&paths.root.join("index.json.sig"), &sig)?;
    state.last_index_seq = idx.seq;
    Ok(idx)
}

/// The last index on disk, checked again (works offline).
pub fn cached_index(paths: &Paths, trusted: &[&str]) -> Result<Index> {
    let data = std::fs::read(paths.root.join("index.json"))?;
    let sig = std::fs::read(paths.root.join("index.json.sig"))?;
    verify_index(&data, &sig, trusted)
}

/// Make sure every file of `m` for `platform` is in the cache with the right sha256. Returns the folder.
pub fn ensure_files(paths: &Paths, m: &ModRelease, platform: &str) -> Result<PathBuf> {
    let part = m.platforms.get(platform).with_context(|| format!("{} has no {platform} files", m.name))?;
    if !safe_name(&m.id) || !safe_name(&m.version) { bail!("bad mod id or version {}", m.key()) }
    let dir = paths.cache_dir(&m.id, &m.version);
    std::fs::create_dir_all(&dir)?;
    for f in &part.files {
        if !safe_name(&f.name) { bail!("{}: bad file name {:?}", m.key(), f.name) }
        let p = dir.join(&f.name);
        if p.exists() && check_file(&p, &f.sha256, f.size).is_ok() { continue; }
        let data = http_get(&f.url)?;
        let tmp = dir.join(format!("{}.part", f.name));
        std::fs::write(&tmp, &data)?;
        check_file(&tmp, &f.sha256, f.size).with_context(|| format!("{}: {} failed the check", m.key(), f.name))?;
        std::fs::rename(&tmp, &p)?;
    }
    Ok(dir)
}
