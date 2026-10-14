//! Pin and update rules (owner, PLAN.md "Launcher"): the player stays on the pinned build; the launcher
//! switches to Steam's new build as soon as every enabled mod has a version for it. The previous copy is
//! kept for one more build. Pure functions: no disk, no network.
use anyhow::{bail, Result};
use serde::Serialize;
use std::collections::BTreeMap;

use crate::model::{Index, ModRelease, State};
use crate::worlds::{effective_mods, pinned_build, Which};

/// Highest version of `id` that supports `build` on `platform`.
pub fn pick<'a>(index: &'a Index, id: &str, build: &str, platform: &str) -> Option<&'a ModRelease> {
    index.mods.iter()
        .filter(|m| m.id == id && m.supports(build, platform))
        .max_by(|a, b| a.semver().cmp(&b.semver()))
}

/// The release of every enabled mod for `build`, or the ids that have none.
pub fn select(index: &Index, enabled: &[String], build: &str, platform: &str)
    -> std::result::Result<Vec<ModRelease>, Vec<String>> {
    let mut out = Vec::new();
    let mut missing = Vec::new();
    for id in enabled {
        match pick(index, id, build, platform) {
            Some(m) => out.push(m.clone()),
            None => missing.push(id.clone()),
        }
    }
    if !index.games.get(build).map_or(false, |g| g.contains_key(platform)) {
        missing.insert(0, "(game build not in the index)".into());
    }
    if missing.is_empty() { Ok(out) } else { Err(missing) }
}

/// `requires` ("id" or "id>=1.2.0") and `conflicts` ("id" or "id<2.0.0") inside one mod set.
pub fn check_set(set: &[ModRelease]) -> Result<()> {
    let by_id: BTreeMap<&str, &ModRelease> = set.iter().map(|m| (m.id.as_str(), m)).collect();
    for m in set {
        for r in &m.requires {
            let (id, req) = split_req(r)?;
            match by_id.get(id) {
                None => bail!("{} needs {} (not enabled)", m.name, r),
                Some(d) if !req.matches(&d.semver()) => bail!("{} needs {}, enabled is {}", m.name, r, d.version),
                _ => {}
            }
        }
        for c in &m.conflicts {
            let (id, req) = split_req(c)?;
            if let Some(d) = by_id.get(id) {
                if req.matches(&d.semver()) { bail!("{} and {} cannot be on together", m.name, d.name) }
            }
        }
    }
    Ok(())
}

fn split_req(s: &str) -> Result<(&str, semver::VersionReq)> {
    let i = s.find(|c: char| "<>=^~".contains(c)).unwrap_or(s.len());
    let (id, r) = s.split_at(i);
    let req = if r.trim().is_empty() { semver::VersionReq::STAR } else { semver::VersionReq::parse(r.trim())? };
    Ok((id.trim(), req))
}

#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum Plan {
    /// Steam has no Dungeon Settlers and no copy exists.
    NoGame,
    /// Steam is downloading an update: wait (its files are half old, half new).
    SteamUpdating { active: Option<String> },
    /// First setup: build the copy for Steam's build.
    Setup { build: String },
    /// No copy yet and some enabled mods have no version for Steam's build.
    Blocked { build: String, missing: Vec<String> },
    /// Copy is on Steam's build.
    Current { build: String },
    /// Steam moved on, mods are not ready: keep playing on the pinned copy.
    Pinned { build: String, steam_build: String, waiting_for: Vec<String> },
    /// Steam moved on and every enabled mod has a version for it: build the new copy, keep the old one.
    Switch { from: String, to: String },
    /// Steam moved on, mods are not ready, and the pinned copy is gone. Options: turn the waiting mods off,
    /// or (later, optional) download the old build through Steam with the player's own login.
    Stuck { build: String, steam_build: String, waiting_for: Vec<String> },
}

#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct Decision {
    pub plan: Plan,
    /// Copies the player may remove (asked first; never removed without a yes).
    pub removable: Vec<String>,
}

/// Plan for one profile (a world, or a new world). `copies` = builds with a copy on disk.
/// `steam_build` = None when Steam has no install. Each world is pinned on its own build.
pub fn decide(steam_build: Option<&str>, steam_updating: bool, state: &State, which: &Which, copies: &[String],
              index: &Index, platform: &str) -> Decision {
    let enabled = effective_mods(state, which);
    let pinned = pinned_build(state, which).map(String::from);
    let has = |b: &str| copies.iter().any(|c| c == b);
    let plan = if steam_updating {
        Plan::SteamUpdating { active: pinned.clone() }
    } else {
        match (steam_build, pinned.as_deref()) {
            (None, None) => Plan::NoGame,
            (None, Some(a)) => {
                if has(a) { Plan::Current { build: a.into() } } else { Plan::NoGame }
            }
            (Some(s), None) => match select(index, &enabled, s, platform) {
                Ok(_) => Plan::Setup { build: s.into() },
                Err(missing) => Plan::Blocked { build: s.into(), missing },
            },
            (Some(s), Some(a)) if s == a => {
                if has(a) { Plan::Current { build: a.into() } } else { Plan::Setup { build: s.into() } }
            }
            (Some(s), Some(a)) => match select(index, &enabled, s, platform) {
                Ok(_) => Plan::Switch { from: a.into(), to: s.into() },
                Err(missing) if has(a) => Plan::Pinned { build: a.into(), steam_build: s.into(), waiting_for: missing },
                Err(missing) => Plan::Stuck { build: a.into(), steam_build: s.into(), waiting_for: missing },
            },
        }
    };
    Decision { plan, removable: removable(state, copies) }
}

/// Copies no world needs: not the newest build, not the one before it, not any world's pinned build.
pub fn removable(state: &State, copies: &[String]) -> Vec<String> {
    let mut keep: Vec<&str> = [state.active_build.as_deref(), state.previous_build.as_deref()].into_iter().flatten().collect();
    keep.extend(state.worlds.values().filter_map(|w| w.build.as_deref()));
    copies.iter().filter(|c| !keep.contains(&c.as_str())).cloned().collect()
}

fn newer(a: &str, b: &str) -> bool {
    match (a.parse::<u64>(), b.parse::<u64>()) { (Ok(x), Ok(y)) => x > y, _ => a > b }
}

/// State after a Play on `to`: the world is pinned there; a newer build becomes the active one and the old
/// active build is kept as the previous one.
pub fn after_switch(state: &State, which: &Which, to: &str) -> State {
    let mut s = state.clone();
    if let Which::World(g) = which { s.worlds.entry(g.clone()).or_default().build = Some(to.into()); }
    match s.active_build.as_deref() {
        Some(a) if a == to || newer(a, to) => {}
        _ => { s.previous_build = s.active_build.take(); s.active_build = Some(to.into()); }
    }
    s
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::{GameStock, Kind, PlatformPart};

    const MAC: &str = "mac-arm64";

    fn rel(id: &str, ver: &str, builds: &[&str]) -> ModRelease {
        let mut platforms = BTreeMap::new();
        platforms.insert(MAC.to_string(), PlatformPart::default());
        ModRelease {
            id: id.into(), name: id.into(), version: ver.into(), author: String::new(), kind: Kind::Code, api: 1,
            scope: Default::default(), difficulty: None, game_builds: builds.iter().map(|s| s.to_string()).collect(), platforms,
            requires: vec![], conflicts: vec![], description: String::new(), dev_only: false,
        }
    }

    fn index(builds: &[&str], mods: Vec<ModRelease>) -> Index {
        let mut games = BTreeMap::new();
        for b in builds {
            let mut p = BTreeMap::new();
            p.insert(MAC.to_string(), GameStock::default());
            games.insert(b.to_string(), p);
        }
        Index { schema: 1, seq: 1, issued: String::new(), games, mods }
    }

    fn state(active: Option<&str>, prev: Option<&str>, enabled: &[&str]) -> State {
        State { active_build: active.map(Into::into), previous_build: prev.map(Into::into),
                new_world: enabled.iter().map(|s| s.to_string()).collect(), ..Default::default() }
    }

    fn v(s: &[&str]) -> Vec<String> { s.iter().map(|x| x.to_string()).collect() }

    #[test]
    fn picks_highest_supported_version() {
        let idx = index(&["100"], vec![rel("mp", "0.9.0", &["100"]), rel("mp", "0.10.0", &["100"]), rel("mp", "0.11.0", &["200"])]);
        assert_eq!(pick(&idx, "mp", "100", MAC).unwrap().version, "0.10.0");
        assert_eq!(pick(&idx, "mp", "200", MAC).unwrap().version, "0.11.0");
        assert!(pick(&idx, "mp", "300", MAC).is_none());
        assert!(pick(&idx, "mp", "100", "win-x64").is_none());
    }

    #[test]
    fn first_run_sets_up_or_blocks() {
        let idx = index(&["100"], vec![rel("mp", "1.0.0", &["100"]), rel("bars", "1.0.0", &["90"])]);
        let d = decide(Some("100"), false, &state(None, None, &["mp"]), &Which::New, &[], &idx, MAC);
        assert_eq!(d.plan, Plan::Setup { build: "100".into() });
        let d = decide(Some("100"), false, &state(None, None, &["mp", "bars"]), &Which::New, &[], &idx, MAC);
        assert_eq!(d.plan, Plan::Blocked { build: "100".into(), missing: v(&["bars"]) });
    }

    #[test]
    fn unknown_game_build_blocks() {
        let idx = index(&["100"], vec![rel("any", "1.0.0", &["*"])]);
        let d = decide(Some("101"), false, &state(None, None, &["any"]), &Which::New, &[], &idx, MAC);
        assert!(matches!(d.plan, Plan::Blocked { .. }));
    }

    #[test]
    fn stays_pinned_until_every_mod_is_ready() {
        let idx = index(&["100", "200"], vec![rel("mp", "1.0.0", &["100"]), rel("mp", "1.1.0", &["200"]), rel("bars", "1.0.0", &["100"])]);
        let st = state(Some("100"), None, &["mp", "bars"]);
        let d = decide(Some("200"), false, &st, &Which::New, &v(&["100"]), &idx, MAC);
        assert_eq!(d.plan, Plan::Pinned { build: "100".into(), steam_build: "200".into(), waiting_for: v(&["bars"]) });
        // the owner's option "update now without <mod>": turn bars off
        let st2 = state(Some("100"), None, &["mp"]);
        let d = decide(Some("200"), false, &st2, &Which::New, &v(&["100"]), &idx, MAC);
        assert_eq!(d.plan, Plan::Switch { from: "100".into(), to: "200".into() });
    }

    #[test]
    fn switches_when_ready_and_keeps_one_old_copy() {
        let idx = index(&["100", "200", "300"], vec![rel("mp", "1.0.0", &["100", "200", "300"])]);
        let st = state(Some("100"), None, &["mp"]);
        let d = decide(Some("200"), false, &st, &Which::New, &v(&["100"]), &idx, MAC);
        assert_eq!(d.plan, Plan::Switch { from: "100".into(), to: "200".into() });
        let st = after_switch(&st, &Which::New, "200");
        assert_eq!((st.active_build.as_deref(), st.previous_build.as_deref()), (Some("200"), Some("100")));
        // on 200, the copy of 100 is kept
        let d = decide(Some("200"), false, &st, &Which::New, &v(&["100", "200"]), &idx, MAC);
        assert_eq!(d.plan, Plan::Current { build: "200".into() });
        assert!(d.removable.is_empty());
        // after 300 is active, 100 may go (asked first), 200 is kept
        let st = after_switch(&st, &Which::New, "300");
        let d = decide(Some("300"), false, &st, &Which::New, &v(&["100", "200", "300"]), &idx, MAC);
        assert_eq!(d.removable, v(&["100"]));
    }

    #[test]
    fn missing_pinned_copy_is_stuck() {
        let idx = index(&["100", "200"], vec![rel("mp", "1.0.0", &["100"])]);
        let d = decide(Some("200"), false, &state(Some("100"), None, &["mp"]), &Which::New, &[], &idx, MAC);
        assert!(matches!(d.plan, Plan::Stuck { .. }));
    }

    #[test]
    fn steam_update_in_progress_waits() {
        let idx = index(&["100"], vec![]);
        let d = decide(Some("100"), true, &state(Some("100"), None, &[]), &Which::New, &v(&["100"]), &idx, MAC);
        assert_eq!(d.plan, Plan::SteamUpdating { active: Some("100".into()) });
    }

    #[test]
    fn no_steam_but_copy_still_plays() {
        let idx = index(&["100"], vec![]);
        let d = decide(None, false, &state(Some("100"), None, &[]), &Which::New, &v(&["100"]), &idx, MAC);
        assert_eq!(d.plan, Plan::Current { build: "100".into() });
        let d = decide(None, false, &state(None, None, &[]), &Which::New, &[], &idx, MAC);
        assert_eq!(d.plan, Plan::NoGame);
    }

    #[test]
    fn each_world_is_pinned_on_its_own() {
        // world a uses only mp (ready for 200); world b also uses bars (not ready for 200)
        let idx = index(&["100", "200"], vec![rel("mp", "1.0.0", &["100", "200"]), rel("bars", "1.0.0", &["100"])]);
        let mut st = state(Some("100"), None, &["mp"]);
        for (g, mods) in [("a", vec!["mp"]), ("b", vec!["mp", "bars"])] {
            st.worlds.insert(g.into(), crate::model::WorldProfile { mods: v(&mods), build: Some("100".into()), ..Default::default() });
        }
        let a = Which::World("a".into());
        let b = Which::World("b".into());
        assert_eq!(decide(Some("200"), false, &st, &a, &v(&["100"]), &idx, MAC).plan, Plan::Switch { from: "100".into(), to: "200".into() });
        let st = after_switch(&st, &a, "200");
        assert_eq!(st.worlds["a"].build.as_deref(), Some("200"));
        assert_eq!((st.active_build.as_deref(), st.previous_build.as_deref()), (Some("200"), Some("100")));
        // world b stays on 100 and plays there
        assert_eq!(decide(Some("200"), false, &st, &b, &v(&["100", "200"]), &idx, MAC).plan,
                   Plan::Pinned { build: "100".into(), steam_build: "200".into(), waiting_for: v(&["bars"]) });
        // 100 is still needed by world b even when 300 comes
        let mut st3 = after_switch(&st, &a, "300");
        assert!(removable(&st3, &v(&["100", "200", "300"])).is_empty());
        st3.worlds.get_mut("b").unwrap().build = Some("300".into());
        assert_eq!(removable(&st3, &v(&["100", "200", "300"])), v(&["100"]));
    }

    #[test]
    fn requires_and_conflicts() {
        let mut a = rel("a", "1.0.0", &["*"]);
        let b = rel("b", "2.1.0", &["*"]);
        a.requires = vec!["b>=2.0.0".into()];
        assert!(check_set(&[a.clone(), b.clone()]).is_ok());
        assert!(check_set(&[a.clone()]).unwrap_err().to_string().contains("not enabled"));
        a.requires = vec!["b>=3.0.0".into()];
        assert!(check_set(&[a.clone(), b.clone()]).is_err());
        a.requires.clear();
        a.conflicts = vec!["b".into()];
        assert!(check_set(&[a.clone(), b.clone()]).unwrap_err().to_string().contains("cannot be on together"));
        a.conflicts = vec!["b<2.0.0".into()];
        assert!(check_set(&[a, b]).is_ok());
    }
}
