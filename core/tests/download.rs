//! Signed index + mod download against a local server (stands in for GitHub Releases).
mod common;
use common::*;
use dsml_core::{fetch, model::State, Paths};

fn setup(seq: u64) -> (std::path::PathBuf, String, Paths) {
    let rel = fresh_dir("dl-release");
    std::fs::write(rel.join("mod.bin"), b"mod payload").unwrap();
    let url = serve(rel.clone());
    let fe = file_entry(&rel.join("mod.bin"), &url);
    let idx = serde_json::json!({ "schema": 1, "seq": seq, "games": {}, "mods": [ {
        "id": "demo", "name": "Demo", "version": "1.0.0", "kind": "code", "game_builds": ["*"],
        "platforms": { "mac-arm64": { "files": [fe] } } } ] });
    publish_index(&rel, &idx);
    (rel, url, Paths::new(fresh_dir("dl-root")))
}

#[test]
fn signed_index_and_verified_download() {
    let (_rel, url, paths) = setup(5);
    let mut st = State::default();
    let pk = dev_pub();
    let idx = fetch::fetch_index(&paths, &format!("{url}/index.json"), &[&pk], &mut st).unwrap();
    assert_eq!(st.last_index_seq, 5);
    let dir = fetch::ensure_files(&paths, &idx.mods[0], "mac-arm64").unwrap();
    assert_eq!(std::fs::read(dir.join("mod.bin")).unwrap(), b"mod payload");
    // the cached copy is checked again offline
    assert_eq!(fetch::cached_index(&paths, &[&pk]).unwrap().seq, 5);
}

#[test]
fn tampered_file_is_refused() {
    let (rel, url, paths) = setup(1);
    let mut st = State::default();
    let pk = dev_pub();
    let idx = fetch::fetch_index(&paths, &format!("{url}/index.json"), &[&pk], &mut st).unwrap();
    std::fs::write(rel.join("mod.bin"), b"mod pAyload").unwrap(); // same size, other bytes
    let err = format!("{:#}", fetch::ensure_files(&paths, &idx.mods[0], "mac-arm64").unwrap_err());
    assert!(err.contains("sha256"), "{err}");
    assert!(!paths.cache_dir("demo", "1.0.0").join("mod.bin").exists());
}

#[test]
fn tampered_index_and_replay_are_refused() {
    let (rel, url, paths) = setup(7);
    let pk = dev_pub();
    let mut st = State { last_index_seq: 8, ..Default::default() };
    let err = fetch::fetch_index(&paths, &format!("{url}/index.json"), &[&pk], &mut st).unwrap_err().to_string();
    assert!(err.contains("older"), "{err}");
    let mut b = std::fs::read(rel.join("index.json")).unwrap();
    let i = b.iter().position(|&c| c == b'7').unwrap();
    b[i] = b'9';
    std::fs::write(rel.join("index.json"), b).unwrap();
    let mut st = State::default();
    let err = fetch::fetch_index(&paths, &format!("{url}/index.json"), &[&pk], &mut st).unwrap_err().to_string();
    assert!(err.contains("NOT valid"), "{err}");
}
