//! Difficulties. Vanilla: the game's five steps; Devastation is locked until the game itself has unlocked it
//! (clear Very Hard). The launcher only READS the game's flag, it never sets it:
//! OutGame.json -> DifficultyUnlockSaveData.IsDownfallUnlocked (class Refactor.Main.DifficultyUnlockSaveData,
//! build 25846389 dump; "Downfall" is the code name of Devastation).
//! Custom: mods with a `difficulty` block (first one: Hell, value 5), shown only when the toggle is on.
use serde::Serialize;
use std::path::Path;

use crate::model::{Index, ModRelease};

/// Stock combat / management difficulty values (enum order in the game).
pub const VANILLA: [(u8, &str); 5] = [(0, "Easy"), (1, "Normal"), (2, "Hard"), (3, "Very Hard"), (4, "Devastation")];
pub const DEVASTATION: u8 = 4;

/// The game's own unlock flag. None = no OutGame.json yet (the game has not run with this folder).
pub fn devastation_unlocked(data_dir: &Path) -> Option<bool> {
    let b = std::fs::read(data_dir.join("OutGame.json")).ok()?;
    let v: serde_json::Value = serde_json::from_slice(&b).ok()?;
    Some(v.pointer("/DifficultyUnlockSaveData/IsDownfallUnlocked").and_then(|x| x.as_bool()).unwrap_or(false))
}

#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct DifficultyOption {
    pub value: u8,
    pub name: String,
    /// The mod that adds it (custom only).
    pub mod_id: Option<String>,
    pub locked: bool,
    /// Why it is locked, in the player's words.
    pub lock_reason: Option<String>,
}

fn latest_by_id<'a>(index: &'a Index) -> Vec<&'a ModRelease> {
    let mut v: Vec<&ModRelease> = Vec::new();
    for m in &index.mods {
        match v.iter().position(|o| o.id == m.id) {
            Some(i) if v[i].semver() < m.semver() => v[i] = m,
            Some(_) => {}
            None => v.push(m),
        }
    }
    v
}

/// The list the launcher shows. `unlocked` = the game's flag (None counts as locked).
pub fn options(index: Option<&Index>, unlocked: Option<bool>, custom_on: bool) -> Vec<DifficultyOption> {
    let open = unlocked == Some(true);
    let reason = || Some("Clear the game on Very Hard to unlock it".to_string());
    let mut out: Vec<DifficultyOption> = VANILLA.iter().map(|(v, n)| DifficultyOption {
        value: *v, name: n.to_string(), mod_id: None,
        locked: *v == DEVASTATION && !open, lock_reason: if *v == DEVASTATION && !open { reason() } else { None },
    }).collect();
    if custom_on {
        if let Some(i) = index {
            let mut customs: Vec<_> = latest_by_id(i).into_iter().filter_map(|m| m.difficulty.as_ref().map(|d| (m, d))).collect();
            customs.sort_by_key(|(_, d)| d.value);
            for (m, d) in customs {
                let locked = d.needs_devastation_unlock && !open;
                out.push(DifficultyOption { value: d.value, name: d.name.clone(), mod_id: Some(m.id.clone()), locked,
                                            lock_reason: if locked { reason() } else { None } });
            }
        }
    }
    out
}

/// The mod a save needs for its difficulty value (None for the stock values).
pub fn mod_for_value(index: &Index, value: i64) -> Option<String> {
    if (0..=DEVASTATION as i64).contains(&value) { return None }
    latest_by_id(index).into_iter().find(|m| m.difficulty.as_ref().map(|d| d.value as i64) == Some(value)).map(|m| m.id.clone())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn index_with_hell() -> Index {
        serde_json::from_value(serde_json::json!({ "schema": 1, "seq": 1, "mods": [
            { "id": "hell-difficulty", "name": "Hell difficulty", "version": "1.0.0", "kind": "patch", "game_builds": ["1"],
              "platforms": { "mac-arm64": {} },
              "difficulty": { "value": 5, "name": "Hell", "needs_devastation_unlock": true } },
            { "id": "multiplayer", "name": "Multiplayer", "version": "1.0.0", "kind": "code", "game_builds": ["1"], "platforms": { "mac-arm64": {} } }
        ] })).unwrap()
    }

    #[test]
    fn reads_the_games_own_unlock_flag() {
        let d = std::env::temp_dir().join(format!("dsml_diff_unlock_{}", std::process::id()));
        std::fs::create_dir_all(&d).unwrap();
        assert_eq!(devastation_unlocked(&d), None);
        std::fs::write(d.join("OutGame.json"), r#"{"TutorialSaveData":{},"DifficultyUnlockSaveData":{"IsDownfallUnlocked":false}}"#).unwrap();
        assert_eq!(devastation_unlocked(&d), Some(false));
        std::fs::write(d.join("OutGame.json"), r#"{"DifficultyUnlockSaveData":{"IsDownfallUnlocked":true}}"#).unwrap();
        assert_eq!(devastation_unlocked(&d), Some(true));
    }

    #[test]
    fn devastation_and_hell_follow_the_unlock() {
        let idx = index_with_hell();
        let locked = options(Some(&idx), Some(false), true);
        assert_eq!(locked.len(), 6);
        assert!(locked[4].locked && locked[4].name == "Devastation");
        assert!(locked[5].locked && locked[5].mod_id.as_deref() == Some("hell-difficulty"));
        assert!(!locked[3].locked);
        let open = options(Some(&idx), Some(true), true);
        assert!(open.iter().all(|o| !o.locked));
        assert!(options(Some(&idx), None, true)[4].locked, "no OutGame.json yet = locked");
    }

    #[test]
    fn custom_difficulties_only_with_the_toggle() {
        let idx = index_with_hell();
        assert_eq!(options(Some(&idx), Some(true), false).len(), 5);
        assert_eq!(options(Some(&idx), Some(true), true).len(), 6);
    }

    #[test]
    fn a_hell_save_needs_the_hell_mod() {
        let idx = index_with_hell();
        assert_eq!(mod_for_value(&idx, 5).as_deref(), Some("hell-difficulty"));
        assert_eq!(mod_for_value(&idx, 4), None);
        assert_eq!(mod_for_value(&idx, 9), None);
    }
}
