//! Bundled mod list: a signed index + its mod files shipped inside the app (Contents/Resources/seed), so the
//! launcher works before any index is published. Layout (made by tools/make_seed.sh):
//! ```text
//! seed/index.json, index.json.sig
//! seed/files/<id>/<version>/<file>
//! ```
//! Installed only when it is newer than the index the launcher already has (same replay rule as a download).
//! The files go into the normal cache, so Play finds them there and never downloads them.
use anyhow::{Context, Result};
use std::path::Path;

use crate::crypto::{check_file, verify_index};
use crate::model::State;
use crate::{safe_name, write_atomic, Paths};

/// Installs the seed in `dir` if it is newer than what the launcher has. `first_run_on`: mods turned on for
/// new worlds when the launcher has no state yet. Returns the installed seq, or None (nothing to do).
pub fn install_seed(paths: &Paths, dir: &Path, trusted: &[&str], platform: &str, first_run_on: &[&str]) -> Result<Option<u64>> {
    let (data, sig) = match (std::fs::read(dir.join("index.json")), std::fs::read(dir.join("index.json.sig"))) {
        (Ok(d), Ok(s)) => (d, s),
        _ => return Ok(None),
    };
    let idx = verify_index(&data, &sig, trusted).context("bundled mod list")?;
    let first_run = !paths.state_file().exists();
    let mut st: State = paths.load_state()?;
    let cached = crate::fetch::cached_index(paths, trusted).map(|i| i.seq).unwrap_or(0);
    if idx.seq <= st.last_index_seq.max(cached) { return Ok(None) }
    for m in &idx.mods {
        let Some(part) = m.platforms.get(platform) else { continue };
        if !safe_name(&m.id) || !safe_name(&m.version) { anyhow::bail!("bundled mod list: bad mod id or version {}", m.key()) }
        let to = paths.cache_dir(&m.id, &m.version);
        for f in &part.files {
            if !safe_name(&f.name) { anyhow::bail!("{}: bad file name {:?}", m.key(), f.name) }
            let dst = to.join(&f.name);
            if dst.exists() && check_file(&dst, &f.sha256, f.size).is_ok() { continue }
            let src = dir.join("files").join(&m.id).join(&m.version).join(&f.name);
            check_file(&src, &f.sha256, f.size).with_context(|| format!("bundled {}: {} failed the check", m.key(), f.name))?;
            std::fs::create_dir_all(&to)?;
            let tmp = to.join(format!("{}.part", f.name));
            std::fs::copy(&src, &tmp)?;
            std::fs::rename(&tmp, &dst)?;
        }
    }
    std::fs::create_dir_all(&paths.root)?;
    write_atomic(&paths.root.join("index.json"), &data)?;
    write_atomic(&paths.root.join("index.json.sig"), &sig)?;
    st.last_index_seq = idx.seq;
    if first_run {
        for id in first_run_on {
            if idx.mods.iter().any(|m| m.id == *id && m.platforms.contains_key(platform)) && !st.new_world.iter().any(|x| x == id) {
                st.new_world.push(id.to_string());
            }
        }
    }
    paths.save_state(&st)?;
    Ok(Some(idx.seq))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn seed(dir: &Path, sk: &str, seq: u64, body: &[u8]) {
        let sha = crate::crypto::sha256_hex(body);
        let idx = serde_json::json!({ "schema": 1, "seq": seq, "mods": [
            { "id": "multiplayer", "name": "Multiplayer", "version": "0.10.0", "kind": "code", "game_builds": ["1"],
              "platforms": { "mac-arm64": { "files": [{ "name": "dsrig.dylib", "sha256": sha, "size": body.len(), "url": "https://bundled.invalid/dsrig.dylib" }] } } } ] });
        let data = serde_json::to_vec(&idx).unwrap();
        std::fs::create_dir_all(dir.join("files/multiplayer/0.10.0")).unwrap();
        std::fs::write(dir.join("files/multiplayer/0.10.0/dsrig.dylib"), body).unwrap();
        std::fs::write(dir.join("index.json"), &data).unwrap();
        std::fs::write(dir.join("index.json.sig"), serde_json::to_vec(&crate::crypto::sign(sk, &data).unwrap()).unwrap()).unwrap();
    }

    #[test]
    fn seed_installs_once_and_turns_multiplayer_on_at_first_run() {
        let t = std::env::temp_dir().join(format!("dsml-seed-{}-{}", std::process::id(), crate::now_secs()));
        let (sk, pk) = crate::crypto::keygen().unwrap();
        let (dir, paths) = (t.join("seed"), Paths::new(t.join("root")));
        seed(&dir, &sk, 10, b"dylib");
        assert_eq!(install_seed(&paths, &dir, &[&pk], "mac-arm64", &["multiplayer"]).unwrap(), Some(10));
        let st = paths.load_state().unwrap();
        assert_eq!(st.new_world, vec!["multiplayer".to_string()]);
        assert_eq!(st.last_index_seq, 10);
        assert_eq!(std::fs::read(paths.cache_dir("multiplayer", "0.10.0").join("dsrig.dylib")).unwrap(), b"dylib");
        assert_eq!(crate::fetch::cached_index(&paths, &[&pk]).unwrap().seq, 10);

        // same seq again: nothing; the player's choice (multiplayer off) stays
        paths.save_state(&State { new_world: vec![], ..st }).unwrap();
        assert_eq!(install_seed(&paths, &dir, &[&pk], "mac-arm64", &["multiplayer"]).unwrap(), None);
        // newer seed: installed, but not a first run, so the mod stays off
        seed(&dir, &sk, 11, b"dylib");
        assert_eq!(install_seed(&paths, &dir, &[&pk], "mac-arm64", &["multiplayer"]).unwrap(), Some(11));
        assert!(paths.load_state().unwrap().new_world.is_empty());

        // a seed signed with another key or with a changed file is refused
        let (sk2, _) = crate::crypto::keygen().unwrap();
        seed(&dir, &sk2, 12, b"dylib");
        assert!(install_seed(&paths, &dir, &[&pk], "mac-arm64", &[]).is_err());
        seed(&dir, &sk, 13, b"dylib");
        std::fs::write(dir.join("files/multiplayer/0.10.0/dsrig.dylib"), b"other").unwrap();
        let _ = std::fs::remove_dir_all(paths.cache_dir("multiplayer", "0.10.0"));
        assert!(install_seed(&paths, &dir, &[&pk], "mac-arm64", &[]).is_err());
        assert_eq!(paths.load_state().unwrap().last_index_seq, 11, "a refused seed changes nothing");
    }
}
