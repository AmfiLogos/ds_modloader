// Dungeon Settlers mod launcher: Tauri shell around dsml-core. The UI is plain HTML in ../ui.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use dsml_core::copy_mac::CopyOptions;
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

fn copy_opts() -> CopyOptions {
    let mut o = CopyOptions::default();
    if let Ok(d) = std::env::var("DSML_STOCK_DIRS") { o.extra_stock_dirs = d.split(':').map(PathBuf::from).collect(); }
    o
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
    let (idx, index_error) = match index { Ok(i) => (Some(i), None), Err(e) => (None, Some(e)) };

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
    tauri::async_runtime::spawn_blocking(move || -> Result<Played, String> {
        let p = paths();
        let st = p.load_state().map_err(err)?;
        let idx = fetch::cached_index(&p, TRUSTED_KEYS).map_err(|e| format!("no checked mod list: {e:#}"))?;
        let inst = install();
        let ready = flow::prepare(&p, inst.as_ref(), &st, &which, &idx, this_platform(), &copy_opts()).map_err(err)?;
        p.save_state(&ready.state).map_err(err)?;
        let spec = launch::LaunchSpec {
            app: ready.app.clone(), profile_dir: p.profile_dir(PROFILE), saves_from: Some(launch::normal_saves()),
            mods: ready.mods.clone(), steam: true, extra_env: BTreeMap::new(), extra_args: vec![], nice: 0,
            world: match &which { Which::World(g) => Some(g.clone()), Which::New => None }, modset: ready.modset.clone(), build: ready.build.clone(),
            session: Some(flow::session(&ready, &which, this_platform())),
        };
        let child = launch::launch(&spec).map_err(err)?;
        Ok(Played { build: ready.build, pid: child.id(), removable: ready.removable })
    }).await.map_err(err)?
}

/// Moves an old copy out of the games folder (to <root>/retired). Only called after the player said yes.
#[tauri::command]
fn remove_copy(which: Which, build: String) -> Result<Status, String> {
    let p = paths();
    let st = p.load_state().map_err(err)?;
    if !dsml_core::decide::removable(&st, &p.copies()).contains(&build) { return Err("this copy is still in use".into()) }
    p.retire(&p.game_dir(&build), &format!("{build}-copy")).map_err(err)?;
    status(which)
}

fn main() {
    use ds_unit_builder::tauri_cmds::*;
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
