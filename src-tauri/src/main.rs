// Dungeon Settlers mod launcher: Tauri shell around dsml-core. The UI is plain HTML in ../ui.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use dsml_core::copy_mac::CopyOptions;
use dsml_core::decide::{pick, Decision};
use dsml_core::model::{this_platform, Index, State};
use dsml_core::{fetch, flow, launch, steam, Paths};
use serde::Serialize;
use std::collections::BTreeMap;
use std::path::PathBuf;

/// Trusted index keys. DEV ONLY: the dev key in dev_keys/ signs test indexes. Before the first public
/// release: replace with the owner's offline key + a rotation key (plan section 5).
const TRUSTED_KEYS: &[&str] = &[include_str!("../../dev_keys/dev.pub")];

#[derive(Serialize)]
struct SteamView { found: bool, build: Option<String>, path: Option<String>, updating: bool }

#[derive(Serialize)]
struct ModView {
    id: String, name: String, description: String, author: String, kind: String, sim: bool,
    enabled: bool, latest: String,
    /// version for the build the player plays on (None = no version for it)
    for_active: Option<String>,
    /// version for Steam's current build
    for_steam: Option<String>,
}

#[derive(Serialize)]
struct Status {
    platform: String, root: String, steam: SteamView, state: State,
    index_seq: Option<u64>, index_error: Option<String>,
    decision: Option<Decision>, mods: Vec<ModView>, copies: Vec<String>,
}

fn paths() -> Paths { Paths::new(Paths::default_root()) }

fn install() -> Option<steam::Install> { steam::find_install(&steam::default_steam_roots()) }

fn copy_opts() -> CopyOptions {
    let mut o = CopyOptions::default();
    if let Ok(d) = std::env::var("DSML_STOCK_DIRS") { o.extra_stock_dirs = d.split(':').map(PathBuf::from).collect(); }
    o
}

fn status_with(index: Result<Index, String>) -> Result<Status, String> {
    let p = paths();
    let state = p.load_state().map_err(|e| e.to_string())?;
    let inst = install();
    let platform = this_platform();
    let (idx, err) = match index { Ok(i) => (Some(i), None), Err(e) => (None, Some(e)) };
    let mut mods = Vec::new();
    let mut decision = None;
    if let Some(idx) = &idx {
        decision = Some(flow::current_decision(&p, inst.as_ref(), &state, idx, platform));
        let mut ids: BTreeMap<String, ()> = BTreeMap::new();
        for m in idx.mods.iter().filter(|m| !m.dev_only || state.dev_mode) { ids.insert(m.id.clone(), ()); }
        for id in ids.keys() {
            let latest = idx.mods.iter().filter(|m| &m.id == id).max_by(|a, b| a.semver().cmp(&b.semver())).unwrap();
            let ver = |b: Option<&String>| b.and_then(|b| pick(idx, id, b, platform)).map(|m| m.version.clone());
            mods.push(ModView {
                id: id.clone(), name: latest.name.clone(), description: latest.description.clone(),
                author: latest.author.clone(), kind: format!("{:?}", latest.kind).to_lowercase(), sim: latest.sim,
                enabled: state.enabled.contains(id), latest: latest.version.clone(),
                for_active: ver(state.active_build.as_ref()),
                for_steam: ver(inst.as_ref().map(|i| &i.build_id)),
            });
        }
    }
    Ok(Status {
        platform: platform.into(), root: p.root.display().to_string(),
        steam: SteamView {
            found: inst.is_some(), build: inst.as_ref().map(|i| i.build_id.clone()),
            path: inst.as_ref().map(|i| i.game_path.display().to_string()), updating: inst.as_ref().map_or(false, |i| i.updating),
        },
        copies: p.copies(), state, index_seq: idx.as_ref().map(|i| i.seq), index_error: err, decision, mods,
    })
}

#[tauri::command]
fn status() -> Result<Status, String> {
    status_with(fetch::cached_index(&paths(), TRUSTED_KEYS).map_err(|e| format!("{e:#}")))
}

#[tauri::command]
async fn refresh_index() -> Result<Status, String> {
    tauri::async_runtime::spawn_blocking(|| {
        let p = paths();
        let mut st = p.load_state().map_err(|e| e.to_string())?;
        let r = fetch::fetch_index(&p, &fetch::index_url(), TRUSTED_KEYS, &mut st);
        if r.is_ok() { p.save_state(&st).map_err(|e| e.to_string())?; }
        let idx = r.or_else(|e| fetch::cached_index(&p, TRUSTED_KEYS).map_err(|_| e)).map_err(|e| format!("{e:#}"));
        status_with(idx)
    }).await.map_err(|e| e.to_string())?
}

#[tauri::command]
fn set_enabled(id: String, on: bool) -> Result<Status, String> {
    let p = paths();
    let mut st = p.load_state().map_err(|e| e.to_string())?;
    st.enabled.retain(|x| x != &id);
    if on { st.enabled.push(id); }
    p.save_state(&st).map_err(|e| e.to_string())?;
    status()
}

#[derive(Serialize)]
struct Played { build: String, pid: u32, removable: Vec<String> }

#[tauri::command]
async fn play() -> Result<Played, String> {
    tauri::async_runtime::spawn_blocking(|| -> Result<Played, String> {
        let p = paths();
        let st = p.load_state().map_err(|e| e.to_string())?;
        let idx = fetch::cached_index(&p, TRUSTED_KEYS).map_err(|e| format!("no checked mod list: {e:#}"))?;
        let inst = install();
        let ready = flow::prepare(&p, inst.as_ref(), &st, &idx, this_platform(), &copy_opts()).map_err(|e| format!("{e:#}"))?;
        p.save_state(&ready.state).map_err(|e| e.to_string())?;
        let spec = launch::LaunchSpec {
            app: ready.app.clone(), profile_dir: p.profile_dir("default"), saves_from: Some(launch::normal_saves()),
            mods: ready.mods.clone(), steam: true, extra_env: BTreeMap::new(), extra_args: vec![], nice: 0,
        };
        let child = launch::launch(&spec).map_err(|e| format!("{e:#}"))?;
        Ok(Played { build: ready.build, pid: child.id(), removable: ready.removable })
    }).await.map_err(|e| e.to_string())?
}

/// Moves an old copy out of the games folder (to <root>/retired). Only called after the player said yes.
#[tauri::command]
fn remove_copy(build: String) -> Result<Status, String> {
    let p = paths();
    let st = p.load_state().map_err(|e| e.to_string())?;
    if st.active_build.as_deref() == Some(&build) || st.previous_build.as_deref() == Some(&build) {
        return Err("this copy is still in use".into());
    }
    p.retire(&p.game_dir(&build), &format!("{build}-copy")).map_err(|e| e.to_string())?;
    status()
}

fn main() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![status, refresh_index, set_enabled, play, remove_copy])
        .run(tauri::generate_context!())
        .expect("error while running the launcher");
}
