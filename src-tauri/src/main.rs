// Dungeon Settlers mod launcher: Tauri shell around dsml-core. The UI is plain HTML in ../ui.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use dsml_core::copy::CopyOptions;
use dsml_core::decide::{pick, Decision};
use dsml_core::difficulty::{self, DifficultyOption};
use dsml_core::model::{this_platform, Index, Scope, State};
use dsml_core::worlds::{self, Which};
use dsml_core::{fetch, flow, launch, steam, Paths};
use serde::Serialize;
use std::collections::BTreeMap;
use std::path::PathBuf;

/// Trusted index keys. DEV ONLY: the dev key in dev_keys/ signs test indexes. Before the first public
/// release: replace with the owner's offline key + a rotation key (plan section 5).
const TRUSTED_KEYS: &[&str] = &[include_str!("../../dev_keys/dev.pub")];
const PROFILE: &str = "default";
/// Turned on for new worlds the first time the launcher runs (the player can turn it off).
const ON_BY_DEFAULT: &[&str] = &["multiplayer"];
/// Why the bundled mod list was not installed (shown instead of "no checked mod list").
static SEED_ERROR: std::sync::OnceLock<String> = std::sync::OnceLock::new();

#[derive(Serialize)]
struct SteamView { found: bool, build: Option<String>, path: Option<String>, updating: bool }

#[derive(Serialize)]
struct ModView {
    id: String, name: String, description: String, author: String, kind: String, scope: Scope,
    /// on for the selected world (directly or through a group / its difficulty)
    enabled: bool, via: Option<String>, latest: String,
    for_pinned: Option<String>, for_steam: Option<String>,
}

#[derive(Serialize)]
struct WorldView { guid: String, name: String, build: Option<String>, combat_difficulty: Option<i64>, custom_difficulty: Option<String> }

#[derive(Serialize)]
struct Status {
    platform: String, root: String, steam: SteamView, state: State, which: Which,
    index_seq: Option<u64>, index_error: Option<String>,
    decision: Option<Decision>, mods: Vec<ModView>, copies: Vec<String>, worlds: Vec<WorldView>,
    devastation_unlocked: Option<bool>, difficulties: Vec<DifficultyOption>,
}

fn paths() -> Paths { Paths::new(Paths::default_root()) }
fn data_dir() -> PathBuf { paths().profile_dir(PROFILE).join("data") }
fn install() -> Option<steam::Install> { steam::find_install(&steam::default_steam_roots()) }
fn err(e: impl std::fmt::Display) -> String { format!("{e:#}") }

/// Verified stock asset copies: DSML_STOCK_DIRS (a path list: ':' Mac, ';' Windows) and <root>/stock_dirs.txt (one folder per line;
/// the owner's Mac, whose live install has mod-changed assets).
fn copy_opts() -> CopyOptions {
    let mut o = CopyOptions::default();
    if let Some(d) = std::env::var_os("DSML_STOCK_DIRS") { o.extra_stock_dirs = std::env::split_paths(&d).collect(); }
    if let Ok(t) = std::fs::read_to_string(paths().root.join("stock_dirs.txt")) {
        o.extra_stock_dirs.extend(t.lines().map(str::trim).filter(|l| !l.is_empty()).map(PathBuf::from));
    }
    o
}

/// The signed mod list shipped in the app (Mac: Contents/Resources/seed; Windows: seed\ next to the exe, where the
/// installer puts the bundle resources; DSML_SEED_DIR overrides).
fn seed_dir() -> Option<PathBuf> {
    if let Some(d) = std::env::var_os("DSML_SEED_DIR") { return Some(d.into()) }
    let exe_dir = std::env::current_exe().ok()?.parent()?.to_path_buf();
    if cfg!(windows) { return Some(exe_dir.join("seed")) }
    Some(exe_dir.parent()?.join("Resources/seed"))
}

fn install_seed() {
    let Some(dir) = seed_dir() else { return };
    if let Err(e) = dsml_core::seed::install_seed(&paths(), &dir, TRUSTED_KEYS, this_platform(), ON_BY_DEFAULT) {
        let _ = SEED_ERROR.set(format!("The mod list inside the launcher could not be used: {e:#}"));
    }
}

fn edit(f: impl FnOnce(&mut State)) -> Result<(), String> {
    let p = paths();
    let mut st = p.load_state().map_err(err)?;
    f(&mut st);
    p.save_state(&st).map_err(err)
}

fn status_with(which: Which, index: Result<Index, String>) -> Result<Status, String> {
    let p = paths();
    let mut state = p.load_state().map_err(err)?;
    let inst = install();
    let platform = this_platform();
    let (idx, index_error) = match index { Ok(i) => (Some(i), None), Err(e) => (None, Some(SEED_ERROR.get().cloned().unwrap_or(e))) };

    // worlds the game made since the last look get the profile they were created with
    let saves = worlds::scan_saves(&data_dir());
    let nw = state.new_world.clone();
    let active = state.active_build.clone();
    if !worlds::register_worlds(&mut state, &saves, &nw, active.as_deref(), idx.as_ref()).is_empty() {
        p.save_state(&state).map_err(err)?;
    }
    let world_views = state.worlds.iter().map(|(g, w)| WorldView {
        guid: g.clone(), name: w.name.clone(), build: w.build.clone(),
        combat_difficulty: saves.iter().find(|s| &s.guid == g).map(|s| s.combat_difficulty),
        custom_difficulty: w.custom_difficulty.clone(),
    }).collect();

    let on = worlds::effective_mods(&state, &which);
    let direct: Vec<String> = match &which {
        Which::New => state.new_world.clone(),
        Which::World(g) => state.worlds.get(g).map(|w| w.mods.clone()).unwrap_or_default(),
    };
    let pinned = worlds::pinned_build(&state, &which).map(String::from);
    let mut mods = Vec::new();
    let mut decision = None;
    if let Some(idx) = &idx {
        decision = Some(flow::current_decision(&p, inst.as_ref(), &state, &which, idx, platform));
        let mut ids: Vec<&str> = Vec::new();
        for m in idx.mods.iter().filter(|m| !m.dev_only || state.dev_mode) { if !ids.contains(&m.id.as_str()) { ids.push(&m.id) } }
        for id in ids {
            let latest = idx.mods.iter().filter(|m| m.id == id).max_by(|a, b| a.semver().cmp(&b.semver())).unwrap();
            if latest.difficulty.is_some() && !state.custom_difficulties && !on.iter().any(|x| x == id) { continue }
            let ver = |b: Option<&String>| b.and_then(|b| pick(idx, id, b, platform)).map(|m| m.version.clone());
            let enabled = on.iter().any(|x| x == id);
            let via = if enabled && !direct.iter().any(|x| x == id) {
                Some(if latest.difficulty.is_some() { "difficulty".to_string() } else { "group".to_string() })
            } else { None };
            mods.push(ModView {
                id: id.into(), name: latest.name.clone(), description: latest.description.clone(),
                author: latest.author.clone(), kind: format!("{:?}", latest.kind).to_lowercase(), scope: latest.scope,
                enabled, via, latest: latest.version.clone(),
                for_pinned: ver(pinned.as_ref()), for_steam: ver(inst.as_ref().map(|i| &i.build_id)),
            });
        }
    }
    let unlocked = difficulty::devastation_unlocked(&data_dir()).or_else(|| difficulty::devastation_unlocked(&launch::normal_saves()));
    Ok(Status {
        platform: platform.into(), root: p.root.display().to_string(),
        steam: SteamView {
            found: inst.is_some(), build: inst.as_ref().map(|i| i.build_id.clone()),
            path: inst.as_ref().map(|i| i.game_path.display().to_string()), updating: inst.as_ref().map_or(false, |i| i.updating),
        },
        copies: p.copies(), worlds: world_views, which,
        difficulties: difficulty::options(idx.as_ref(), unlocked, state.custom_difficulties),
        devastation_unlocked: unlocked,
        state, index_seq: idx.as_ref().map(|i| i.seq), index_error, decision, mods,
    })
}

#[tauri::command]
fn status(which: Which) -> Result<Status, String> {
    status_with(which, fetch::cached_index(&paths(), TRUSTED_KEYS).map_err(err))
}

#[tauri::command]
async fn refresh_index(which: Which) -> Result<Status, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let p = paths();
        let mut st = p.load_state().map_err(err)?;
        let r = fetch::fetch_index(&p, &fetch::index_url(), TRUSTED_KEYS, &mut st);
        if r.is_ok() { p.save_state(&st).map_err(err)?; }
        let idx = r.or_else(|e| fetch::cached_index(&p, TRUSTED_KEYS).map_err(|_| e)).map_err(err);
        status_with(which, idx)
    }).await.map_err(err)?
}

#[tauri::command]
fn set_mod(which: Which, id: String, on: bool) -> Result<Status, String> {
    edit(|st| worlds::set_mod(st, &which, &id, on))?;
    status(which)
}

/// Save a group (folder) of mods. An empty list removes the group from the list (worlds keep their name of it).
#[tauri::command]
fn save_group(which: Which, name: String, mods: Vec<String>) -> Result<Status, String> {
    let name = name.trim().to_string();
    if name.is_empty() { return Err("a group needs a name".into()) }
    edit(|st| { if mods.is_empty() { st.groups.remove(&name); } else { st.groups.insert(name, mods); } })?;
    status(which)
}

#[tauri::command]
fn apply_group(which: Which, name: String, on: bool) -> Result<Status, String> {
    edit(|st| worlds::set_group(st, &which, &name, on))?;
    status(which)
}

#[tauri::command]
fn set_custom_difficulties(which: Which, on: bool) -> Result<Status, String> {
    edit(|st| st.custom_difficulties = on)?;
    status(which)
}

/// New world only: pick a custom difficulty mod (None = the game's own difficulties).
#[tauri::command]
fn set_new_world_difficulty(mod_id: Option<String>) -> Result<Status, String> {
    let idx = fetch::cached_index(&paths(), TRUSTED_KEYS).map_err(err)?;
    edit(|st| {
        st.new_world.retain(|id| !idx.mods.iter().any(|m| &m.id == id && m.difficulty.is_some()));
        if let Some(m) = mod_id { st.new_world.push(m) }
    })?;
    status(Which::New)
}

/// Host rule of a world: "mods" (Allow mods) or "vanilla" (Vanilla check).
#[tauri::command]
fn set_match_rule(which: Which, rule: dsml_core::join::MatchRule) -> Result<Status, String> {
    let Which::World(g) = &which else { return Err("the rule belongs to a world".into()) };
    edit(|st| st.worlds.entry(g.clone()).or_default().match_rule = rule)?;
    status(which)
}

#[derive(Serialize)]
struct Played { build: String, pid: u32, removable: Vec<String> }

#[tauri::command]
async fn play(which: Which) -> Result<Played, String> {
    tauri::async_runtime::spawn_blocking(move || play_blocking(which, vec![])).await.map_err(err)?
}

/// Play: copy ready (made if needed), Steam started if it is closed (Windows), game started. `extra_args`: tests.
fn play_blocking(which: Which, extra_args: Vec<String>) -> Result<Played, String> {
    let p = paths();
    let st = p.load_state().map_err(err)?;
    let idx = fetch::cached_index(&p, TRUSTED_KEYS).map_err(|e| format!("no checked mod list: {e:#}"))?;
    let inst = install();
    let ready = flow::prepare(&p, inst.as_ref(), &st, &which, &idx, this_platform(), &copy_opts()).map_err(err)?;
    p.save_state(&ready.state).map_err(err)?;
    let spec = launch::LaunchSpec {
        app: ready.app.clone(), profile_dir: p.profile_dir(PROFILE), saves_from: Some(launch::normal_saves()),
        mods: ready.mods.clone(), steam: true, extra_env: BTreeMap::new(), extra_args, nice: 0,
        world: match &which { Which::World(g) => Some(g.clone()), Which::New => None }, modset: ready.modset.clone(), build: ready.build.clone(),
        session: Some(flow::session(&ready, &which, this_platform())),
    };
    let child = launch::launch(&spec).map_err(err)?;
    Ok(Played { build: ready.build, pid: child.id(), removable: ready.removable })
}

/// Removes an old copy (Mac: moved to <root>/retired; Windows: deleted, a full copy is ~700 MB). Only called after
/// the player said yes.
#[tauri::command]
fn remove_copy(which: Which, build: String) -> Result<Status, String> {
    let p = paths();
    let st = p.load_state().map_err(err)?;
    if !dsml_core::decide::removable(&st, &p.copies()).contains(&build) { return Err("this copy is still in use".into()) }
    p.remove_copy(&build).map_err(err)?;
    status(which)
}

/// DSML_SMOKE=status|prepare|play: the launcher's own code path with no window (tests; see core/tests/e2e_mac.rs
/// owner_app_smoke, tools/win_smoke.sh). status = what the mod list shows for a new world; prepare = Play up to the
/// ready copy, not started; play = the Play button for a new world, game arguments from DSML_GAME_ARGS (tests:
/// "-batchmode -nographics").
fn smoke(what: &str) -> Result<serde_json::Value, String> {
    match what {
        "status" => status(Which::New).and_then(|s| serde_json::to_value(s).map_err(err)),
        "prepare" => {
            let p = paths();
            let st = p.load_state().map_err(err)?;
            let idx = fetch::cached_index(&p, TRUSTED_KEYS).map_err(err)?;
            let ready = flow::prepare(&p, install().as_ref(), &st, &Which::New, &idx, this_platform(), &copy_opts()).map_err(err)?;
            p.save_state(&ready.state).map_err(err)?;
            let session = flow::session(&ready, &Which::New, this_platform());
            Ok(serde_json::json!({ "build": ready.build, "app": ready.app, "modset": ready.modset, "mods": ready.mods, "session": session }))
        }
        "play" => {
            let args = std::env::var("DSML_GAME_ARGS").unwrap_or_default().split_whitespace().map(String::from).collect();
            play_blocking(Which::New, args).and_then(|r| serde_json::to_value(r).map_err(err))
        }
        _ => Err(format!("DSML_SMOKE={what}: status, prepare or play")),
    }
}

fn main() {
    use ds_unit_builder::tauri_cmds::*;
    install_seed();
    if let Ok(what) = std::env::var("DSML_SMOKE") {
        match smoke(&what) {
            Ok(v) => { println!("{v}"); std::process::exit(0) }
            Err(e) => { eprintln!("{e}"); std::process::exit(1) }
        }
    }
    tauri::Builder::default()
        // Unit Builder: "my units" live in <launcher root>/builder/units.json
        .manage(UnitBuilderState::new(paths().root))
        .invoke_handler(tauri::generate_handler![status, refresh_index, set_mod, save_group, apply_group,
                                                 set_custom_difficulties, set_new_world_difficulty, set_match_rule, play, remove_copy,
                                                 builder_catalog, builder_new_unit, builder_compute, builder_validate,
                                                 builder_set_item, builder_set_stack_level, builder_decode_link, builder_encode_link,
                                                 units_list, units_save, units_rename, units_duplicate, units_delete, units_import_link,
                                                 teams_create, teams_update, teams_delete, units_export])
        .run(tauri::generate_context!())
        .expect("error while running the launcher");
}
