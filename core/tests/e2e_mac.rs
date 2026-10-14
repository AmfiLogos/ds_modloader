//! End to end on this Mac: fake Steam library (APFS clone of the real app, read only) -> signed index on a
//! local server -> Setup decision -> verified download -> base copy -> play copy -> hidden launch ->
//! the multiplayer dylib answers on its control socket. The real install, saves and prefs are checked
//! unchanged at the end.
//!
//! Run: cargo test -p dsml-core --test e2e_mac -- --ignored --nocapture
//! Hidden launch = rig.py's method: -batchmode + DSRIG_BATCH=1, LSBackgroundOnly copy, own HOME, and a
//! guard that kills the game at once if it ever shows a window or becomes the front app.
mod common;
use common::*;
use dsml_core::copy_mac::{base_app, play_app, CopyOptions};
use dsml_core::decide::Plan;
use dsml_core::{fetch, flow, launch, model::State, steam, Paths};
use std::collections::BTreeMap;
use std::io::{BufRead, BufReader, Write};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::{Duration, Instant};

const BUILD: &str = "25846389";

fn home() -> PathBuf { PathBuf::from(std::env::var("HOME").unwrap()) }
fn real_steam() -> PathBuf { home().join("Library/Application Support/Steam") }
fn real_app() -> PathBuf { real_steam().join("steamapps/common/Dungeon Settlers/DungeonSettlers.app") }
fn sha(p: &Path) -> String { dsml_core::crypto::sha256_file(p).unwrap_or_else(|_| "missing".into()) }

/// Fake Steam library in test_env/steam: clone of the real app (read only) + copies of the manifests.
fn fake_steam() -> PathBuf {
    let root = launcher_dir().join("test_env/steam");
    let app = root.join("steamapps/common/Dungeon Settlers/DungeonSettlers.app");
    if !app.exists() {
        std::fs::create_dir_all(app.parent().unwrap()).unwrap();
        let st = Command::new("/bin/cp").args(["-c", "-R"]).arg(real_app()).arg(&app).status().unwrap();
        assert!(st.success(), "clone of the game failed");
    }
    let acf = std::fs::read_to_string(real_steam().join(format!("steamapps/appmanifest_{}.acf", steam::APP_ID))).unwrap();
    std::fs::write(root.join(format!("steamapps/appmanifest_{}.acf", steam::APP_ID)), acf).unwrap();
    let vdf = format!("\"libraryfolders\"\n{{\n\t\"0\"\n\t{{\n\t\t\"path\"\t\t\"{}\"\n\t}}\n}}\n", root.display());
    std::fs::write(root.join("steamapps/libraryfolders.vdf"), vdf).unwrap();
    root
}

fn winlist(pid: u32) -> Vec<serde_json::Value> {
    let out = Command::new(home().join("ds_multiplayer/rig/tools/winlist")).arg(pid.to_string()).output().unwrap();
    serde_json::from_slice(&out.stdout).unwrap_or_default()
}

fn front_pid() -> Option<u32> {
    let asn = Command::new("lsappinfo").arg("front").output().ok()?;
    let asn = String::from_utf8_lossy(&asn.stdout).trim().to_string();
    let info = Command::new("lsappinfo").args(["info", "-only", "pid", &asn]).output().ok()?;
    let s = String::from_utf8_lossy(&info.stdout);
    s.split('=').nth(1)?.trim().parse().ok()
}

fn ask(sock: &Path, cmd: &str) -> std::io::Result<String> {
    let mut s = std::os::unix::net::UnixStream::connect(sock)?;
    s.set_read_timeout(Some(Duration::from_secs(30)))?;
    s.write_all(format!("{{\"cmd\":\"{cmd}\"}}\n").as_bytes())?;
    let mut line = String::new();
    BufReader::new(s).read_line(&mut line)?;
    Ok(line)
}

#[test]
#[ignore]
fn side_copy_and_hidden_launch() {
    let t0 = Instant::now();
    let prefs = home().join("Library/Preferences/com.CanOpener.Dungeon-Settlers.plist");
    let user_setting = home().join("Library/Application Support/CanOpener/Dungeon Settlers/UserSetting.json");
    let real_ga = real_app().join("Contents/Frameworks/GameAssembly.dylib");
    let before = (sha(&prefs), sha(&user_setting), sha(&real_ga));

    // 1. fake Steam + build detection
    let steam_root = fake_steam();
    let inst = steam::find_install(&[steam_root.clone()]).expect("game in the fake library");
    assert_eq!(inst.build_id, BUILD);
    assert!(inst.game_path.starts_with(&steam_root));
    println!("build detected: {} at {}", inst.build_id, inst.game_path.display());

    // 2. release: the current multiplayer dylib + its ranges file, and a DEV-ONLY patch mod that turns
    //    the game's Steam requirement off (as rig/make_instance.sh does), so the test never talks to Steam
    let rel = fresh_dir("e2e-release");
    let mp_build = home().join("ds_multiplayer/build");
    for f in ["dsrig.dylib", "det_ranges.tsv"] { std::fs::copy(mp_build.join(f), rel.join(f)).unwrap(); }
    let url = serve(rel.clone());
    let stock_ga = inst.game_path.join(format!("Contents/Frameworks/GameAssembly.dylib.stock_{BUILD}"));
    let idx = serde_json::json!({
        "schema": 1, "seq": 1, "issued": "test",
        "games": { BUILD: { "mac-arm64": { "gameassembly_sha256": sha(&stock_ga) } } },
        "mods": [
          { "id": "multiplayer", "name": "Multiplayer", "version": "0.9.0", "kind": "code", "sim": true,
            "game_builds": [BUILD], "platforms": { "mac-arm64": { "files": [] } } },
          { "id": "multiplayer", "name": "Multiplayer", "version": "0.10.0", "kind": "code", "sim": true,
            "game_builds": [BUILD], "description": "Play one world together (test build)",
            "platforms": { "mac-arm64": {
              "files": [file_entry(&rel.join("dsrig.dylib"), &url), file_entry(&rel.join("det_ranges.tsv"), &url)],
              "inject": ["dsrig.dylib"],
              "env": { "DSRIG_DIR": "{profile_dir}", "DSRIG_FPS": "60", "DSRIG_LIVE": "1" } } } },
          { "id": "test-steam-gate-off", "name": "TEST: no Steam", "version": "1.0.0", "kind": "patch", "dev_only": true,
            "game_builds": [BUILD],
            "platforms": { "mac-arm64": { "patches": { BUILD: [
              { "file": "Contents/Frameworks/GameAssembly.dylib", "offset": "0xD25730", "expect": "20008052", "new": "00008052" } ] } } } }
        ] });
    publish_index(&rel, &idx);

    // 3. launcher state + signed index
    let paths = Paths::new(fresh_dir("e2e-root"));
    let pk = dev_pub();
    let mut st = State { enabled: vec!["multiplayer".into(), "test-steam-gate-off".into()], dev_mode: true, ..Default::default() };
    let index = fetch::fetch_index(&paths, &format!("{url}/index.json"), &[&pk], &mut st).unwrap();
    let d = flow::current_decision(&paths, Some(&inst), &st, &index, "mac-arm64");
    assert_eq!(d.plan, Plan::Setup { build: BUILD.into() });

    // 4. copies (the owner's live install has mod-changed assets: verified stock copies put them back)
    let opts = CopyOptions {
        bundle_id: "com.CanOpener.Dungeon-Settlers.DSMLtest".into(), name: "DSML test".into(), background_only: true,
        extra_stock_dirs: vec![home().join("ds_weaponslot/b25846389/assets/work/stock_aa")],
    };
    let ready = flow::prepare(&paths, Some(&inst), &st, &index, "mac-arm64", &opts).unwrap();
    println!("copies ready in {:?}: {}", t0.elapsed(), ready.app.display());
    assert_eq!(ready.state.active_build.as_deref(), Some(BUILD));
    assert_eq!(ready.mods.iter().find(|m| m.id == "multiplayer").unwrap().version, "0.10.0");
    assert!(base_app(&paths, BUILD).exists());
    let app = play_app(&paths, BUILD);
    let fw = app.join("Contents/Frameworks");
    assert_eq!(sha(&fw.join("dsrig.dylib")), sha(&rel.join("dsrig.dylib")));
    assert_eq!(sha(&fw.join("det_ranges.tsv")), sha(&rel.join("det_ranges.tsv")), "ranges file reachable next to the dylib");
    let ga = std::fs::read(fw.join("GameAssembly.dylib")).unwrap();
    let (off, _) = dsml_core::macho::arm64_slice(&ga).unwrap();
    assert_eq!(hex::encode(&ga[off + 0xD25730..off + 0xD25734]), "00008052", "patch applied");
    assert!(Command::new("/usr/bin/codesign").arg("-v").arg(&app).status().unwrap().success());
    // base stays stock
    assert_eq!(sha(&base_app(&paths, BUILD).join("Contents/Frameworks/GameAssembly.dylib")), sha(&stock_ga));
    paths.save_state(&ready.state).unwrap();

    // second Play with the same mods: nothing is rebuilt
    let again = flow::prepare(&paths, Some(&inst), &ready.state, &index, "mac-arm64", &opts).unwrap();
    assert!(!paths.retired().exists(), "the play copy was rebuilt although nothing changed");
    assert_eq!(again.app, app);

    // 5. hidden launch
    let profile = paths.profile_dir("test");
    let mut env = BTreeMap::new();
    for (k, v) in [("DSRIG_LIVE", ""), ("DSRIG_BATCH", "1"), ("DSRIG_FPS", "30")] { env.insert(k.to_string(), v.to_string()); }
    env.insert("HOME".into(), profile.join("home").display().to_string());
    let spec = launch::LaunchSpec {
        app: app.clone(), profile_dir: profile.clone(), saves_from: None, mods: ready.mods.clone(), steam: false,
        extra_env: env,
        extra_args: ["-batchmode", "-screen-fullscreen", "0", "-screen-width", "960", "-screen-height", "540"]
            .iter().map(|s| s.to_string()).collect(),
        nice: 5,
    };
    let e = launch::env_for(&spec).unwrap();
    assert!(e["DYLD_INSERT_LIBRARIES"].ends_with("play/DungeonSettlers.app/Contents/Frameworks/dsrig.dylib"));
    assert_eq!(e["DSRIG_DIR"], profile.display().to_string());
    assert!(!e.contains_key("DSRIG_LIVE"));
    let mut child = launch::launch(&spec).unwrap();
    let pid = child.id();
    println!("game started, pid {pid}");

    let sock = profile.join("ctl.sock");
    let rig_log = profile.join("rig.log");
    let mut answer = None;
    let mut guard_kill = None;
    let start = Instant::now();
    while start.elapsed() < Duration::from_secs(180) {
        let vis: Vec<_> = winlist(pid).into_iter()
            .filter(|w| w["onscreen"].as_bool() == Some(true) && w["alpha"].as_f64().unwrap_or(0.0) > 0.0).collect();
        if !vis.is_empty() || front_pid() == Some(pid) {
            unsafe { libc::kill(pid as i32, libc::SIGKILL); }
            guard_kill = Some(format!("visible={vis:?}"));
            break;
        }
        if let Ok(Some(s)) = child.try_wait() { panic!("game ended early: {s}; see {}", profile.display()); }
        if sock.exists() {
            if let Ok(a) = ask(&sock, "status") { answer = Some(a); break; }
        }
        std::thread::sleep(Duration::from_millis(500));
    }
    let log = std::fs::read_to_string(&rig_log).unwrap_or_default();
    // stop it: quit over the socket, then kill by PID
    let _ = ask(&sock, "quit");
    let t = Instant::now();
    while t.elapsed() < Duration::from_secs(10) && child.try_wait().ok().flatten().is_none() { std::thread::sleep(Duration::from_millis(200)); }
    if child.try_wait().ok().flatten().is_none() { unsafe { libc::kill(pid as i32, libc::SIGKILL); } let _ = child.wait(); }

    assert!(guard_kill.is_none(), "GUARD: the game showed a window: {guard_kill:?}");
    let answer = answer.expect("no answer on the control socket within 180 s");
    println!("rig.log: {}", log.lines().find(|l| l.contains("rig start pid")).unwrap_or("(none)"));
    println!("socket status answer: {}", answer.trim());
    assert!(log.contains(&format!("rig start pid {pid}")), "dylib did not log its start for pid {pid}");
    let v: serde_json::Value = serde_json::from_str(answer.trim()).expect("status answer is JSON");
    assert!(v.get("scene").is_some(), "status has no scene: {v}");

    // 6. owner's files unchanged
    let after = (sha(&prefs), sha(&user_setting), sha(&real_ga));
    assert_eq!(before, after, "owner's prefs / settings / real GameAssembly changed");
    println!("owner's prefs, UserSetting.json and real GameAssembly unchanged; total {:?}", t0.elapsed());
}
