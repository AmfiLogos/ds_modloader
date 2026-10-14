//! Mods per world. A world = one campaign, identified by `CampaignSaveHeader.CampaignGuid` in its save
//! files (the same guid is in the save file names). Saves are only READ here.
use anyhow::{Context, Result};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use crate::model::{Index, State, WorldProfile};

/// Which profile a Play uses.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "kind", content = "guid", rename_all = "snake_case")]
pub enum Which {
    /// A new campaign: the `new_world` profile.
    New,
    World(String),
}

#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct SaveInfo {
    pub guid: String,
    pub clan: String,
    pub combat_difficulty: i64,
    pub management_difficulty: i64,
    pub save_time: String,
    pub file: PathBuf,
}

#[derive(Deserialize)]
#[serde(rename_all = "PascalCase")]
struct Header {
    campaign_guid: String,
    #[serde(default)]
    clan_name: String,
    #[serde(default)]
    combat_difficulty: i64,
    #[serde(default)]
    management_difficulty: i64,
    #[serde(default)]
    save_time: String,
}

#[derive(Deserialize)]
struct SaveHead {
    #[serde(rename = "CampaignSaveHeader")]
    header: Header,
}

/// The newest save of every world in `<data>/Saves`.
pub fn scan_saves(data_dir: &Path) -> Vec<SaveInfo> {
    let mut by: BTreeMap<String, SaveInfo> = BTreeMap::new();
    let Ok(rd) = std::fs::read_dir(data_dir.join("Saves")) else { return vec![] };
    for e in rd.flatten() {
        let p = e.path();
        let name = p.file_name().and_then(|n| n.to_str()).unwrap_or("");
        if !name.ends_with(".json") || name.starts_with("mp_sync") { continue }
        let Ok(h) = read_header(&p) else { continue };
        let s = SaveInfo { guid: h.campaign_guid, clan: h.clan_name, combat_difficulty: h.combat_difficulty,
                           management_difficulty: h.management_difficulty, save_time: h.save_time, file: p };
        match by.get(&s.guid) {
            Some(o) if o.save_time >= s.save_time => {}
            _ => { by.insert(s.guid.clone(), s); }
        }
    }
    by.into_values().collect()
}

fn read_header(p: &Path) -> Result<Header> {
    let f = std::fs::File::open(p)?;
    let h: SaveHead = serde_json::from_reader(std::io::BufReader::new(f)).with_context(|| format!("{}", p.display()))?;
    Ok(h.header)
}

/// Mod ids a profile turns on: its own mods, then the members of its groups, then its custom difficulty.
pub fn effective_mods(state: &State, which: &Which) -> Vec<String> {
    let (mods, groups, diff): (&[String], &[String], Option<&String>) = match which {
        Which::New => (&state.new_world, &[], None),
        Which::World(g) => match state.worlds.get(g) {
            Some(w) => (&w.mods, &w.groups, w.custom_difficulty.as_ref()),
            None => (&state.new_world, &[], None),
        },
    };
    let mut out: Vec<String> = Vec::new();
    let mut add = |id: &String| if !out.contains(id) { out.push(id.clone()) };
    mods.iter().for_each(&mut add);
    for g in groups { if let Some(m) = state.groups.get(g) { m.iter().for_each(&mut add) } }
    if let Some(d) = diff { add(d) }
    out
}

/// The build a profile is pinned to (a new world uses the newest build in use).
pub fn pinned_build<'a>(state: &'a State, which: &Which) -> Option<&'a str> {
    match which {
        Which::World(g) => state.worlds.get(g).and_then(|w| w.build.as_deref()).or(state.active_build.as_deref()),
        Which::New => state.active_build.as_deref(),
    }
}

/// Worlds found in the saves that the launcher does not know yet get the profile they were created with
/// (`created_with`: the new-world mods of the copy that made them). A save on a custom difficulty
/// (CombatDifficulty above the stock 4) gets the mod that adds that difficulty. Returns the new guids.
pub fn register_worlds(state: &mut State, saves: &[SaveInfo], created_with: &[String], build: Option<&str>,
                       index: Option<&Index>) -> Vec<String> {
    let mut added = Vec::new();
    let new_modset = state.new_world_modset.clone();
    for s in saves {
        let custom = index.and_then(|i| crate::difficulty::mod_for_value(i, s.combat_difficulty));
        let w = state.worlds.entry(s.guid.clone()).or_insert_with(|| {
            added.push(s.guid.clone());
            WorldProfile { name: s.clan.clone(), mods: created_with.to_vec(), build: build.map(Into::into),
                           modset: new_modset.clone(), ..Default::default() }
        });
        if w.name.is_empty() { w.name = s.clan.clone(); }
        if w.custom_difficulty.is_none() { w.custom_difficulty = custom; }
    }
    added
}

/// Turn one mod on or off for a profile.
pub fn set_mod(state: &mut State, which: &Which, id: &str, on: bool) {
    let list = match which {
        Which::New => &mut state.new_world,
        Which::World(g) => &mut state.worlds.entry(g.clone()).or_default().mods,
    };
    list.retain(|x| x != id);
    if on { list.push(id.into()); }
}

/// Apply (or take off) a group for a profile. For a new world the group's members are copied in.
pub fn set_group(state: &mut State, which: &Which, group: &str, on: bool) {
    match which {
        Which::New => {
            let members = state.groups.get(group).cloned().unwrap_or_default();
            for m in members { set_mod(state, which, &m, on) }
        }
        Which::World(g) => {
            let w = state.worlds.entry(g.clone()).or_default();
            w.groups.retain(|x| x != group);
            if on { w.groups.push(group.into()); }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn save(dir: &Path, name: &str, guid: &str, clan: &str, combat: i64, time: &str) {
        std::fs::create_dir_all(dir.join("Saves")).unwrap();
        let j = serde_json::json!({ "EntityContainerSaveData": {"big": [1, 2, 3]},
            "CampaignSaveHeader": { "CampaignGuid": guid, "ClanName": clan, "CombatDifficulty": combat,
                                    "ManagementDifficulty": 3, "SaveTime": time } });
        std::fs::write(dir.join("Saves").join(name), serde_json::to_vec(&j).unwrap()).unwrap();
    }

    #[test]
    fn scans_newest_save_per_world() {
        let d = std::env::temp_dir().join(format!("dsml_worlds_{}", std::process::id()));
        save(&d, "Elven__AutoSave_a_0.json", "a", "Elven", 3, "2026-10-11 18:25:31");
        save(&d, "Elven__Manual_a_1.json", "a", "Elven", 3, "2026-10-12 09:00:00");
        save(&d, "Moon__AutoSave_b_0.json", "b", "Moon", 5, "2026-10-04 19:56:00");
        std::fs::write(d.join("Saves/steam_autocloud.vdf"), "x").unwrap();
        std::fs::write(d.join("Saves/broken.json"), "{").unwrap();
        let s = scan_saves(&d);
        assert_eq!(s.len(), 2);
        assert_eq!(s[0].guid, "a");
        assert!(s[0].file.ends_with("Elven__Manual_a_1.json"));
        assert_eq!(s[1].combat_difficulty, 5);
    }

    #[test]
    fn profiles_are_per_world_with_groups_and_difficulty() {
        let mut st = State::default();
        st.new_world = vec!["multiplayer".into()];
        st.groups.insert("comfort".into(), vec!["notch-fix".into(), "multi-unit-bars".into()]);
        let saves = vec![
            SaveInfo { guid: "a".into(), clan: "Elven".into(), combat_difficulty: 3, management_difficulty: 3, save_time: "1".into(), file: "x".into() },
        ];
        let nw = st.new_world.clone();
        assert_eq!(register_worlds(&mut st, &saves, &nw, Some("100"), None), vec!["a".to_string()]);
        let a = Which::World("a".into());
        set_group(&mut st, &a, "comfort", true);
        set_mod(&mut st, &a, "cheaper-recruits", true);
        st.worlds.get_mut("a").unwrap().custom_difficulty = Some("hell-difficulty".into());
        assert_eq!(effective_mods(&st, &a), vec!["multiplayer", "cheaper-recruits", "notch-fix", "multi-unit-bars", "hell-difficulty"]);
        // the new-world profile is not touched by world a
        assert_eq!(effective_mods(&st, &Which::New), vec!["multiplayer"]);
        // a second world keeps its own list
        set_mod(&mut st, &Which::World("b".into()), "tougher-monsters", true);
        assert_eq!(effective_mods(&st, &Which::World("b".into())), vec!["tougher-monsters"]);
        assert_eq!(pinned_build(&st, &a), Some("100"));
        // taking the group off removes its members only
        set_group(&mut st, &a, "comfort", false);
        assert_eq!(effective_mods(&st, &a), vec!["multiplayer", "cheaper-recruits", "hell-difficulty"]);
        // registering again does not reset a known world
        register_worlds(&mut st, &saves, &[], Some("200"), None);
        assert_eq!(st.worlds["a"].build.as_deref(), Some("100"));
    }

    #[test]
    fn old_state_with_global_list_still_loads() {
        let st: State = serde_json::from_str(r#"{"active_build":"1","enabled":["multiplayer"],"last_index_seq":2}"#).unwrap();
        assert_eq!(st.new_world, vec!["multiplayer"]);
    }
}
