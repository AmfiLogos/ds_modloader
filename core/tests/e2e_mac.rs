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
use dsml_core::copy_mac::{base_app, CopyOptions};
use dsml_core::decide::Plan;
use dsml_core::worlds::Which;
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

fn owner_files() -> (String, String, String) {
    let prefs = home().join("Library/Preferences/com.CanOpener.Dungeon-Settlers.plist");
    let user_setting = home().join("Library/Application Support/CanOpener/Dungeon Settlers/UserSetting.json");
    let real_ga = real_app().join("Contents/Frameworks/GameAssembly.dylib");
    (sha(&prefs), sha(&user_setting), sha(&real_ga))
}

fn opts() -> CopyOptions {
    CopyOptions {
        bundle_id: "com.CanOpener.Dungeon-Settlers.DSMLtest".into(), name: "DSML test".into(), background_only: true,
        extra_stock_dirs: vec![home().join("ds_weaponslot/b25846389/assets/work/stock_aa")],
    }
}

/// Starts the copy hidden (rig.py method), waits until `ready(status)` holds, stops it. Returns the last
/// status answer and the rig.log text. Panics if a window ever shows.
fn hidden_run(app: &Path, profile: &Path, mods: &[dsml_core::model::ModRelease], world: Option<String>, modset: &str,
              ready: impl Fn(&serde_json::Value) -> bool, limit: Duration) -> (serde_json::Value, String, u32) {
    let mut env = BTreeMap::new();
    for (k, v) in [("DSRIG_LIVE", ""), ("DSRIG_BATCH", "1"), ("DSRIG_FPS", "30")] { env.insert(k.to_string(), v.to_string()); }
    env.insert("HOME".into(), profile.join("home").display().to_string());
    let spec = launch::LaunchSpec {
        app: app.to_path_buf(), profile_dir: profile.to_path_buf(), saves_from: None, mods: mods.to_vec(), steam: false,
        extra_env: env,
        extra_args: ["-batchmode", "-screen-fullscreen", "0", "-screen-width", "960", "-screen-height", "540"]
            .iter().map(|s| s.to_string()).collect(),
        nice: 5, world, modset: modset.into(),
    };
    let e = launch::env_for(&spec).unwrap();
    assert!(e["DYLD_INSERT_LIBRARIES"].ends_with("/DungeonSettlers.app/Contents/Frameworks/dsrig.dylib"));
    assert_eq!(e["DSRIG_DIR"], profile.display().to_string());
    assert_eq!(e["DSML_MODSET"], modset);
    assert!(!e.contains_key("DSRIG_LIVE"));
    let mut child = launch::launch(&spec).unwrap();
    let pid = child.id();
    println!("game started, pid {pid}");
    let sock = profile.join("ctl.sock");
    let mut answer = None;
    let mut guard_kill = None;
    let start = Instant::now();
    while start.elapsed() < limit {
        let vis: Vec<_> = winlist(pid).into_iter()
            .filter(|w| w["onscreen"].as_bool() == Some(true) && w["alpha"].as_f64().unwrap_or(0.0) > 0.0).collect();
        if !vis.is_empty() || front_pid() == Some(pid) {
            unsafe { libc::kill(pid as i32, libc::SIGKILL); }
            guard_kill = Some(format!("visible={vis:?}"));
            break;
        }
        if let Ok(Some(s)) = child.try_wait() { panic!("game ended early: {s}; see {}", profile.display()); }
        if sock.exists() {
            if let Ok(a) = ask(&sock, "status") {
                if let Ok(v) = serde_json::from_str::<serde_json::Value>(a.trim()) {
                    let done = ready(&v);
                    answer = Some(v);
                    if done { break }
                }
            }
        }
        std::thread::sleep(Duration::from_millis(1000));
    }
    let log = std::fs::read_to_string(profile.join("rig.log")).unwrap_or_default();
    let _ = ask(&sock, "quit");
    let t = Instant::now();
    while t.elapsed() < Duration::from_secs(10) && child.try_wait().ok().flatten().is_none() { std::thread::sleep(Duration::from_millis(200)); }
    if child.try_wait().ok().flatten().is_none() { unsafe { libc::kill(pid as i32, libc::SIGKILL); } let _ = child.wait(); }
    assert!(guard_kill.is_none(), "GUARD: the game showed a window: {guard_kill:?}");
    (answer.expect("no answer on the control socket"), log, pid)
}

#[test]
#[ignore]
fn side_copy_and_hidden_launch() {
    let t0 = Instant::now();
    let before = owner_files();

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
    let mut st = State { new_world: vec!["multiplayer".into(), "test-steam-gate-off".into()], dev_mode: true, ..Default::default() };
    let index = fetch::fetch_index(&paths, &format!("{url}/index.json"), &[&pk], &mut st).unwrap();
    let d = flow::current_decision(&paths, Some(&inst), &st, &Which::New, &index, "mac-arm64");
    assert_eq!(d.plan, Plan::Setup { build: BUILD.into() });

    // 4. copies (the owner's live install has mod-changed assets: verified stock copies put them back)
    let opts = opts();
    let ready = flow::prepare(&paths, Some(&inst), &st, &Which::New, &index, "mac-arm64", &opts).unwrap();
    println!("copies ready in {:?}: {}", t0.elapsed(), ready.app.display());
    assert_eq!(ready.state.active_build.as_deref(), Some(BUILD));
    assert_eq!(ready.mods.iter().find(|m| m.id == "multiplayer").unwrap().version, "0.10.0");
    assert!(base_app(&paths, BUILD).exists());
    let app = ready.app.clone();
    assert!(app.starts_with(paths.game_dir(BUILD)) && app.parent().unwrap().file_name().unwrap().to_string_lossy().starts_with("play-"));
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
    let again = flow::prepare(&paths, Some(&inst), &ready.state, &Which::New, &index, "mac-arm64", &opts).unwrap();
    assert!(!paths.retired().exists(), "the play copy was rebuilt although nothing changed");
    assert_eq!(again.app, app);

    // 5. hidden launch
    let profile = paths.profile_dir("test");
    let (v, log, pid) = hidden_run(&app, &profile, &ready.mods, None, &ready.modset, |_| true, Duration::from_secs(180));
    println!("rig.log: {}", log.lines().find(|l| l.contains("rig start pid")).unwrap_or("(none)"));
    println!("socket status answer: {v}");
    assert!(log.contains(&format!("rig start pid {pid}")), "dylib did not log its start for pid {pid}");
    assert!(v.get("scene").is_some(), "status has no scene: {v}");

    // 6. owner's files unchanged
    assert_eq!(before, owner_files(), "owner's prefs / settings / real GameAssembly changed");
    println!("owner's prefs, UserSetting.json and real GameAssembly unchanged; total {:?}", t0.elapsed());
}

const AA: &str = "Contents/Resources/Data/StreamingAssets/aa";
const GA: &str = "Contents/Frameworks/GameAssembly.dylib";

/// A patch mod made the way the publisher would: GameAssembly + catalog.bin as byte patches (so mods that
/// touch other bytes compose), big asset bundles as whole-file replacements.
fn patch_mod(rel: &Path, url: &str, id: &str, name: &str, scope: &str, stock_ga: &Path, modded_ga: &Path,
             catalog: (&Path, &Path), bundles: &[(&Path, &str)], difficulty: Option<serde_json::Value>) -> serde_json::Value {
    let mut patches = dsml_core::copy_mac::diff_patches(&std::fs::read(stock_ga).unwrap(), &std::fs::read(modded_ga).unwrap(), GA, "arm64").unwrap();
    patches.extend(dsml_core::copy_mac::diff_patches(&std::fs::read(catalog.0).unwrap(), &std::fs::read(catalog.1).unwrap(),
                                                     &format!("{AA}/catalog.bin"), "file").unwrap());
    let mut files = Vec::new();
    for (src, dest) in bundles {
        let n = src.file_name().unwrap();
        std::fs::copy(src, rel.join(n)).unwrap();
        let mut fe = file_entry(&rel.join(n), url);
        fe["dest"] = serde_json::json!(dest);
        files.push(fe);
    }
    let mut m = serde_json::json!({ "id": id, "name": name, "version": "1.0.0", "kind": "patch", "scope": scope,
        "game_builds": [BUILD], "platforms": { "mac-arm64": { "files": files, "patches": { BUILD: patches } } } });
    if let Some(d) = difficulty { m["difficulty"] = d; }
    m
}

#[test]
#[ignore]
fn worlds_hell_and_client_mod_compose() {
    let before = owner_files();
    let steam_root = fake_steam();
    let inst = steam::find_install(&[steam_root]).unwrap();
    let rel = fresh_dir("e2e2-release");
    let url = serve(rel.clone());
    let mp_build = home().join("ds_multiplayer/build");
    for f in ["dsrig.dylib", "det_ranges.tsv"] { std::fs::copy(mp_build.join(f), rel.join(f)).unwrap(); }
    let stock_ga = inst.game_path.join(format!("Contents/Frameworks/GameAssembly.dylib.stock_{BUILD}"));
    let hell = home().join("DungeonSettlers_HellDifficulty/b25846389");
    let ws = home().join("ds_weaponslot/b25846389");
    let ws_ga = launcher_dir().join("test_env/modsrc/ws/GameAssembly.dylib");
    assert!(ws_ga.exists(), "make it first: patch_weaponslot.py apply <stock> {}", ws_ga.display());
    let table = "table_assets_all_ee685f7db1494c1e6bf42e948951b8dc.bundle";
    let ui = "uiprefab_assets_all_836f445db0845d9456ec1355aac5cbbb.bundle";
    let hell_mod = patch_mod(&rel, &url, "hell-difficulty", "Hell difficulty", "gameplay", &stock_ga,
        &hell.join("staged/GameAssembly.hell_on_stock.dylib"), (&hell.join("stock/catalog.bin"), &hell.join("staged/aa/catalog.bin")),
        &[(&hell.join("staged/aa").join(table), &format!("{AA}/StandaloneOSX/{table}"))],
        Some(serde_json::json!({ "value": 5, "name": "Hell", "needs_devastation_unlock": true })));
    let ws_mod = patch_mod(&rel, &url, "weapon-skill-slot", "Weapon-skill slot", "client", &stock_ga, &ws_ga,
        (&ws.join("assets/work/stock_aa/catalog.bin"), &ws.join("staged/catalog.bin")),
        &[(&ws.join("staged").join(ui), &format!("{AA}/StandaloneOSX/{ui}"))], None);
    let n_patches = |m: &serde_json::Value| m["platforms"]["mac-arm64"]["patches"][BUILD].as_array().unwrap().len();
    println!("hell: {} patches, weapon slot: {} patches", n_patches(&hell_mod), n_patches(&ws_mod));
    let idx = serde_json::json!({ "schema": 1, "seq": 1, "games": { BUILD: { "mac-arm64": { "gameassembly_sha256": sha(&stock_ga) } } },
        "mods": [ hell_mod, ws_mod,
          { "id": "multiplayer", "name": "Multiplayer", "version": "0.10.0", "kind": "code", "scope": "gameplay", "game_builds": [BUILD],
            "platforms": { "mac-arm64": { "files": [file_entry(&rel.join("dsrig.dylib"), &url), file_entry(&rel.join("det_ranges.tsv"), &url)],
              "inject": ["dsrig.dylib"], "env": { "DSRIG_DIR": "{profile_dir}", "DSRIG_LIVE": "1" } } } },
          { "id": "test-steam-gate-off", "name": "TEST: no Steam", "version": "1.0.0", "kind": "patch", "dev_only": true,
            "game_builds": [BUILD], "platforms": { "mac-arm64": { "patches": { BUILD: [
              { "file": GA, "offset": "0xD25730", "expect": "20008052", "new": "00008052" } ] } } } } ] });
    publish_index(&rel, &idx);

    let paths = Paths::new(fresh_dir("e2e2-root"));
    let pk = dev_pub();
    let mut st = State { new_world: vec!["multiplayer".into(), "test-steam-gate-off".into()], ..Default::default() };
    let index = fetch::fetch_index(&paths, &format!("{url}/index.json"), &[&pk], &mut st).unwrap();

    // two worlds found in the saves: a Hell campaign (CombatDifficulty 5) and a Very Hard one
    let profile = paths.profile_dir("test");
    std::fs::create_dir_all(profile.join("data/Saves")).unwrap();
    for (g, clan, cd) in [("hellworld", "Hell Clan", 5), ("plainworld", "Plain Clan", 3)] {
        let j = serde_json::json!({ "CampaignSaveHeader": { "CampaignGuid": g, "ClanName": clan, "CombatDifficulty": cd, "SaveTime": "2026-10-14 12:00:00" } });
        std::fs::write(profile.join(format!("data/Saves/{clan}_AutoSave_{g}_0.json")), serde_json::to_vec(&j).unwrap()).unwrap();
    }
    let saves = dsml_core::worlds::scan_saves(&profile.join("data"));
    let nw = st.new_world.clone();
    let added = dsml_core::worlds::register_worlds(&mut st, &saves, &nw, None, Some(&index));
    assert_eq!(added.len(), 2);
    assert_eq!(st.worlds["hellworld"].custom_difficulty.as_deref(), Some("hell-difficulty"), "Hell save needs the Hell mod");
    assert_eq!(st.worlds["plainworld"].custom_difficulty, None);
    let hw = Which::World("hellworld".into());
    let pw = Which::World("plainworld".into());
    dsml_core::worlds::set_mod(&mut st, &hw, "weapon-skill-slot", true);

    let opts = opts();
    let r_hell = flow::prepare(&paths, Some(&inst), &st, &hw, &index, "mac-arm64", &opts).unwrap();
    let st = r_hell.state.clone();
    let r_plain = flow::prepare(&paths, Some(&inst), &st, &pw, &index, "mac-arm64", &opts).unwrap();
    assert_ne!(r_hell.app, r_plain.app, "each mod set has its own play copy");
    assert_eq!(r_hell.mods.iter().map(|m| m.id.as_str()).collect::<Vec<_>>(),
               vec!["multiplayer", "test-steam-gate-off", "weapon-skill-slot", "hell-difficulty"]);
    assert_eq!(r_plain.mods.len(), 2);

    // gameplay hash: Hell counts, the client weapon slot does not
    let no_ws: Vec<_> = r_hell.mods.iter().filter(|m| m.id != "weapon-skill-slot").cloned().collect();
    assert_eq!(r_hell.modset, dsml_core::join::modset_hash(&no_ws, BUILD, "mac-arm64"));
    assert_ne!(r_hell.modset, r_plain.modset);

    // every byte of both mods is in the Hell copy; the plain copy has none of them
    let read = |app: &Path, rel: &str| std::fs::read(app.join(rel)).unwrap();
    let check = |app: &Path, m: &dsml_core::model::ModRelease, want: bool| {
        let ps = &m.platforms["mac-arm64"].patches[BUILD];
        let ga = read(app, GA);
        let (off, _) = dsml_core::macho::arm64_slice(&ga).unwrap();
        let cat = read(app, &format!("{AA}/catalog.bin"));
        for p in ps {
            let o = usize::from_str_radix(p.offset.trim_start_matches("0x"), 16).unwrap();
            let cur = if p.slice == "arm64" { &ga[off + o..off + o + p.new.len() / 2] } else { &cat[o..o + p.new.len() / 2] };
            assert_eq!(&hex::encode(cur), if want { &p.new } else { &p.expect }, "{} {} {}", m.id, p.file, p.offset);
        }
    };
    let m = |id: &str| index.mods.iter().find(|x| x.id == id).unwrap().clone();
    check(&r_hell.app, &m("hell-difficulty"), true);
    check(&r_hell.app, &m("weapon-skill-slot"), true);
    check(&r_plain.app, &m("hell-difficulty"), false);
    check(&r_plain.app, &m("weapon-skill-slot"), false);
    assert_eq!(sha(&r_hell.app.join(format!("{AA}/StandaloneOSX/{table}"))), sha(&hell.join("staged/aa").join(table)));
    assert_eq!(sha(&r_hell.app.join(format!("{AA}/StandaloneOSX/{ui}"))), sha(&ws.join("staged").join(ui)));
    for a in [&r_hell.app, &r_plain.app] { assert!(Command::new("/usr/bin/codesign").arg("-v").arg(a).status().unwrap().success()); }
    println!("hell world copy: {}\nplain world copy: {}", r_hell.app.display(), r_plain.app.display());

    // the composed Hell + weapon-slot + multiplayer copy boots to the title screen (hidden)
    let (v, log, pid) = hidden_run(&r_hell.app, &profile, &r_hell.mods, Some("hellworld".into()), &r_hell.modset,
                                   |v| v["scene"].as_i64() == Some(1) && v["busy"].as_bool() == Some(false), Duration::from_secs(300));
    assert!(log.contains(&format!("rig start pid {pid}")));
    assert_eq!(v["scene"].as_i64(), Some(1), "did not reach the title screen: {v}");
    println!("Hell + weapon slot copy reached the title screen: scene {} frames {}", v["scene"], v["frames"]);
    assert_eq!(before, owner_files());
}

/// Copies for the weapon-slot pair test (tests/weaponslot_pair.py): host = weapon slot + no-Steam patch,
/// guest = no-Steam patch only. Each with the rig's own bundle id for its instance letter (own prefs domain).
/// Env PAIR=EF (the leased pair). Writes test_env/pair/<X>/DungeonSettlers.app.
#[test]
#[ignore]
fn make_pair_copies() {
    let pair = std::env::var("PAIR").expect("PAIR=<leased pair, e.g. EF>");
    let (h, g) = (&pair[0..1], &pair[1..2]);
    let steam_root = fake_steam();
    let inst = steam::find_install(&[steam_root]).unwrap();
    let rel = fresh_dir("pair-release");
    let url = serve(rel.clone());
    let stock_ga = inst.game_path.join(format!("Contents/Frameworks/GameAssembly.dylib.stock_{BUILD}"));
    let ws = home().join("ds_weaponslot/b25846389");
    let ui = "uiprefab_assets_all_836f445db0845d9456ec1355aac5cbbb.bundle";
    let ws_mod = patch_mod(&rel, &url, "weapon-skill-slot", "Weapon-skill slot", "client", &stock_ga,
        &launcher_dir().join("test_env/modsrc/ws/GameAssembly.dylib"),
        (&ws.join("assets/work/stock_aa/catalog.bin"), &ws.join("staged/catalog.bin")),
        &[(&ws.join("staged").join(ui), &format!("{AA}/StandaloneOSX/{ui}"))], None);
    let idx = serde_json::json!({ "schema": 1, "seq": 1, "games": { BUILD: { "mac-arm64": { "gameassembly_sha256": sha(&stock_ga) } } },
        "mods": [ ws_mod,
          { "id": "test-steam-gate-off", "name": "TEST: no Steam", "version": "1.0.0", "kind": "patch", "dev_only": true,
            "game_builds": [BUILD], "platforms": { "mac-arm64": { "patches": { BUILD: [
              { "file": GA, "offset": "0xD25730", "expect": "20008052", "new": "00008052" } ] } } } } ] });
    publish_index(&rel, &idx);
    let paths = Paths::new(launcher_dir().join("test_env/pair/launcher_root"));
    let mut st = State { new_world: vec!["test-steam-gate-off".into()], ..Default::default() };
    let index = fetch::fetch_index(&paths, &format!("{url}/index.json"), &[&dev_pub()], &mut st).unwrap();
    st.worlds.insert("host".into(), dsml_core::model::WorldProfile { mods: vec!["test-steam-gate-off".into(), "weapon-skill-slot".into()], ..Default::default() });
    st.worlds.insert("guest".into(), dsml_core::model::WorldProfile { mods: vec!["test-steam-gate-off".into()], ..Default::default() });
    for (x, w) in [(h, "host"), (g, "guest")] {
        let mut o = opts();
        o.bundle_id = format!("com.CanOpener.Dungeon-Settlers.mp{x}");
        o.name = format!("DS pair {x}");
        let r = flow::prepare(&paths, Some(&inst), &st, &Which::World(w.into()), &index, "mac-arm64", &o).unwrap();
        let dst = launcher_dir().join(format!("test_env/pair/{x}/DungeonSettlers.app"));
        if dst.exists() { paths.retire(&dst, &format!("pair-{x}")).unwrap(); }
        std::fs::create_dir_all(dst.parent().unwrap()).unwrap();
        assert!(Command::new("/bin/cp").args(["-c", "-R"]).arg(&r.app).arg(&dst).status().unwrap().success());
        println!("{w} {x}: {} mods {:?}", dst.display(), r.mods.iter().map(|m| m.key()).collect::<Vec<_>>());
    }
}
