//! Tauri 2 commands for the Unit Builder UI (feature "tauri"). App commands, not a plugin: the launcher lists them
//! in its `generate_handler!` and manages one `UnitBuilderState` (NOTES.md, "Wiring into the launcher").
//! Every command answers `Result<_, String>`; the error text is written for the player.
use crate::codec::{self, Decoded};
use crate::compute::{self, Estimates, Sheet, SkillSets};
use crate::model::Build;
use crate::store::{Store, Team, Unit};
use crate::validate::validate;
use serde::Serialize;
use serde_json::Value;
use std::path::PathBuf;
use std::sync::Mutex;
use tauri::State;

pub struct UnitBuilderState {
    pub root: PathBuf,
    store: Mutex<Option<Store>>,
}

impl UnitBuilderState {
    /// `root` = the launcher folder (dsml_core::Paths root); the units file is `<root>/builder/units.json`
    pub fn new(root: PathBuf) -> Self {
        UnitBuilderState { root, store: Mutex::new(None) }
    }
    pub fn units_path(&self) -> PathBuf {
        self.root.join("builder").join("units.json")
    }
    pub fn exports_dir(&self) -> PathBuf {
        self.root.join("builder").join("exports")
    }
    fn with<T>(&self, f: impl FnOnce(&mut Store) -> Result<T, String>) -> Result<T, String> {
        let mut g = self.store.lock().map_err(|_| "The units file is busy. Try again.".to_string())?;
        if g.is_none() {
            *g = Some(Store::open(&self.units_path()).map_err(|e| e.to_string())?);
        }
        f(g.as_mut().unwrap())
    }
    fn with_save<T>(&self, f: impl FnOnce(&mut Store) -> Result<T, String>) -> Result<T, String> {
        self.with(|s| {
            let backup = s.file.clone();
            let r = f(s);
            if r.is_ok() {
                if let Err(e) = s.save() {
                    s.file = backup;
                    return Err(e.to_string());
                }
            } else {
                s.file = backup;
            }
            r
        })
    }
}

/// the builder payload (names, icons, descriptions, pools, trees, items...) for the UI, plus the data's game build
#[tauri::command]
pub fn builder_catalog() -> Value {
    let d = crate::data::data();
    serde_json::json!({ "gameBuild": d.d.game_build(), "gameVersion": d.d.game_version(), "wikiUrl": codec::WIKI_BUILDER_URL, "dsb": d.raw })
}

/// a fresh unit: the builder's default build (Human, level cap of the builder, Moderate talents), a recruit name
#[tauri::command]
pub fn builder_new_unit(race: Option<String>) -> Build {
    let d = crate::data::dsb();
    let mut b = Build::default_build();
    if let Some(r) = race.filter(|r| d.races.iter().any(|x| &x.k == r)) {
        b.portrait = d.portraits.get(&r).and_then(|p| p.first().cloned());
        b.race = Some(r);
    }
    let short = b.race.as_deref().unwrap_or("").trim_start_matches("AFFECTER_").to_string();
    let pool = d.playernames.get(&short).or(d.playernames.get("Human"));
    b.name = Some(match pool.filter(|p| !p.is_empty()) {
        Some(p) => p[(crate::store::now_ms() as usize) % p.len()].clone(),
        None => "New Character".into(),
    });
    validate(&mut b);
    b
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SkillCost {
    pub skill: String,
    pub tree: Option<String>,
    pub cost: Option<f64>,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Live {
    pub sheet: Sheet,
    pub estimates: Estimates,
    pub estimates_magic: Estimates,
    pub main_sp: f64,
    pub sub_sp: f64,
    pub main_sp_used: f64,
    pub sub_sp_used: f64,
    pub insc_slots: f64,
    pub insc_cap: f64,
    pub insc_free: f64,
    pub active_trees: Vec<String>,
    /// the cost of EVERY skill of the unit's trees with this sheet (learned or not)
    pub skill_costs: Vec<SkillCost>,
    pub skill_sets: SkillSets,
    pub quick: Vec<String>,
    /// milestone passives: [skill, bonus level, max level, learned]
    pub stack_passives: Vec<(String, usize, usize, bool)>,
    pub two_handed: bool,
}

/// the live sheet of a build (call after every change; validate first so the build is legal)
#[tauri::command]
pub fn builder_compute(build: Build) -> Live {
    let d = crate::data::dsb();
    let sheet = compute::compute(&build);
    let st = &sheet.st;
    let trees = compute::active_trees(&build);
    let mut costs = vec![];
    for t in &trees {
        for row in &d.trees[t].rows {
            for k in row.iter().flatten() {
                costs.push(SkillCost { skill: k.clone(), tree: Some(t.clone()), cost: Some(compute::skill_cost(k, t, st)) });
            }
        }
    }
    let (mut mu, mut su) = (0.0, 0.0);
    for k in &build.learned {
        if let Some(t) = compute::tree_of_skill_for(k, &build) {
            if compute::tree_is_weapon(&t) {
                mu += compute::skill_cost(k, &t, st)
            } else if let Some(s) = d.skills.get(k) {
                su += s.sp
            }
        }
    }
    let stack = compute::stack_passives(&build).iter().map(|sp| (sp.k.clone(), compute::stack_lv_for(&build, &sp.k), sp.max, sp.learned)).collect();
    Live {
        estimates: compute::estimates(st, false),
        estimates_magic: compute::estimates(st, true),
        main_sp: compute::main_sp_budget(&build),
        sub_sp: compute::sub_sp_budget(&build),
        main_sp_used: mu,
        sub_sp_used: su,
        insc_slots: compute::insc_slots(&build),
        insc_cap: compute::insc_cap(&build),
        insc_free: compute::insc_free(&build),
        active_trees: trees,
        skill_costs: costs,
        skill_sets: compute::unit_skill_sets(&build, None),
        quick: compute::quick_for(&build),
        stack_passives: stack,
        two_handed: build.is_two_handed(),
        sheet,
    }
}

/// the builder's rules applied (returns the corrected build)
#[tauri::command]
pub fn builder_validate(mut build: Build) -> Build {
    validate(&mut build);
    build
}

/// put an item in a slot the way the builder does (rolls of the outgoing item are kept for later), then validate
#[tauri::command]
pub fn builder_set_item(mut build: Build, slot: String, item: Option<String>) -> Build {
    crate::validate::set_slot_item(&mut build, &slot, item.as_deref());
    validate(&mut build);
    build
}

/// set a milestone passive's bonus level (Sharpshooter, Deep Water)
#[tauri::command]
pub fn builder_set_stack_level(mut build: Build, skill: String, level: usize) -> Build {
    let d = crate::data::dsb();
    let Some(pg) = d.passive_grants.get(&skill) else { return build };
    let Some(thr) = pg.thr.as_ref().filter(|t| !t.is_empty()) else { return build };
    let lv = level.min(thr.len());
    build.stack_lv.get_or_insert_with(Default::default).insert(skill.clone(), Some(serde_json::json!(lv)));
    build.passive_stacks.get_or_insert_with(Default::default).insert(skill, Some(if lv > 0 { thr[lv - 1] } else { 0.0 }));
    build
}

/// any builder link (team, one unit, full #b= link) or bare code -> its units
#[tauri::command]
pub fn builder_decode_link(text: String) -> Result<Decoded, String> {
    codec::decode_link(&text).map_err(|e| e.to_string())
}

#[derive(Serialize)]
pub struct Links {
    pub code: String,
    /// ?team=CODE (opens the team on the wiki; the PvP "Paste Wiki Link" box takes it)
    pub team: String,
    /// #b=~CODE (one unit only)
    pub solo: Option<String>,
}
#[tauri::command]
pub fn builder_encode_link(builds: Vec<Build>) -> Result<Links, String> {
    if builds.is_empty() || builds.len() > codec::MAX_TEAM {
        return Err(format!("A link holds 1 to {} units.", codec::MAX_TEAM));
    }
    let code = codec::encode_team(&builds);
    Ok(Links {
        team: format!("{}?team={code}", codec::WIKI_BUILDER_URL),
        solo: (builds.len() == 1).then(|| format!("{}#b=~{code}", codec::WIKI_BUILDER_URL)),
        code,
    })
}

#[tauri::command]
pub fn units_list(state: State<UnitBuilderState>) -> Result<(Vec<Unit>, Vec<Team>), String> {
    state.with(|s| Ok((s.file.units.clone(), s.file.teams.clone())))
}
#[tauri::command]
pub fn units_save(state: State<UnitBuilderState>, id: Option<String>, build: Build) -> Result<Unit, String> {
    state.with_save(|s| s.put(id.as_deref(), build).map_err(|e| e.to_string()))
}
#[tauri::command]
pub fn units_rename(state: State<UnitBuilderState>, id: String, name: String) -> Result<Unit, String> {
    state.with_save(|s| s.rename(&id, &name).map_err(|e| e.to_string()))
}
#[tauri::command]
pub fn units_duplicate(state: State<UnitBuilderState>, id: String) -> Result<Unit, String> {
    state.with_save(|s| s.duplicate(&id).map_err(|e| e.to_string()))
}
#[tauri::command]
pub fn units_delete(state: State<UnitBuilderState>, id: String) -> Result<(), String> {
    state.with_save(|s| s.delete(&id).map_err(|e| e.to_string()))
}
/// add the units of a builder link to "my units"
#[tauri::command]
pub fn units_import_link(state: State<UnitBuilderState>, text: String) -> Result<Vec<Unit>, String> {
    let d = codec::decode_link(&text).map_err(|e| e.to_string())?;
    let builds = d
        .units
        .into_iter()
        .map(|mut b| {
            validate(&mut b);
            b
        })
        .collect();
    state.with_save(|s| s.import(builds).map_err(|e| e.to_string()))
}
#[tauri::command]
pub fn teams_create(state: State<UnitBuilderState>, name: String, unit_ids: Vec<String>) -> Result<Team, String> {
    state.with_save(|s| s.team_create(&name, unit_ids).map_err(|e| e.to_string()))
}
#[tauri::command]
pub fn teams_update(state: State<UnitBuilderState>, id: String, name: Option<String>, unit_ids: Option<Vec<String>>) -> Result<Team, String> {
    state.with_save(|s| s.team_update(&id, name.as_deref(), unit_ids).map_err(|e| e.to_string()))
}
#[tauri::command]
pub fn teams_delete(state: State<UnitBuilderState>, id: String) -> Result<(), String> {
    state.with_save(|s| s.team_delete(&id).map_err(|e| e.to_string()))
}

/// export saved units for PvP / Foreign Expedition: checks that the mod can build every unit (same rules and
/// refusal texts as ext/pvp pvp_build.c), writes `<root>/builder/exports/<name>.json` and returns the team link
#[tauri::command]
pub fn units_export(state: State<UnitBuilderState>, unit_ids: Vec<String>, name: Option<String>) -> Result<crate::handoff::Exported, String> {
    let builds: Vec<Build> = state.with(|s| {
        unit_ids.iter().map(|id| s.get(id).map(|u| u.build.clone()).ok_or_else(|| format!("No unit with id {id}."))).collect()
    })?;
    let file_name = name.unwrap_or_else(|| "units".into());
    crate::handoff::export_to_dir(&builds, &state.exports_dir(), &file_name)
}

