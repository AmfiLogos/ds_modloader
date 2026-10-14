//! Multiplayer mod check on the launcher side. Only GAMEPLAY mods count: they are compared, hashed and
//! downloaded (from the signed index only, never from the host). CLIENT mods are never required from
//! others, never hashed, and each player keeps their own.
//! The in-game HELLO check (multiplayer mod, src/net) sends `modset_hash`; that part is in the MP source.
use anyhow::{bail, Result};
use serde::{Deserialize, Serialize};

use crate::decide::pick;
use crate::model::{Index, ModRelease, Scope};

#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
pub struct ModRef {
    pub id: String,
    pub version: String,
}

impl ModRef {
    pub fn of(m: &ModRelease) -> Self { ModRef { id: m.id.clone(), version: m.version.clone() } }
}

pub fn gameplay(mods: &[ModRelease]) -> Vec<&ModRelease> { mods.iter().filter(|m| m.scope == Scope::Gameplay).collect() }

/// sha256 over the sorted gameplay mods: `id@version` + every file sha256 + the patches of this build.
pub fn modset_hash(mods: &[ModRelease], build: &str, platform: &str) -> String {
    let mut lines: Vec<String> = gameplay(mods).into_iter().map(|m| {
        let mut l = m.key();
        if let Some(p) = m.platforms.get(platform) {
            let mut shas: Vec<&str> = p.files.iter().map(|f| f.sha256.as_str()).collect();
            shas.sort();
            l.push(':');
            l.push_str(&shas.join(","));
            if let Some(ps) = p.patches.get(build) {
                l.push(':');
                l.push_str(&crate::crypto::sha256_hex(&serde_json::to_vec(ps).unwrap_or_default()));
            }
        }
        l
    }).collect();
    lines.sort();
    crate::crypto::sha256_hex(lines.join("\n").as_bytes())
}

/// The host's rule for a match (set per world). Shown in the lobby / PvP screens.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Default)]
#[serde(rename_all = "lowercase")]
pub enum MatchRule {
    /// "Allow mods": the host's gameplay mods are required; joiners get them from the signed index.
    #[default]
    Mods,
    /// "Vanilla check": nobody may run a gameplay mod. Client mods are still allowed.
    Vanilla,
}

/// What a host announces: the rule, its gameplay mods and their hash. In-game code reads it from the
/// session file (`session_file`) and sends it in HELLO.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct HostOffer {
    pub rule: MatchRule,
    pub gameplay: Vec<ModRef>,
    pub modset: String,
}

/// Host side: a vanilla match cannot be hosted with gameplay mods on.
pub fn host_offer(rule: MatchRule, mods: &[ModRelease], build: &str, platform: &str) -> Result<HostOffer> {
    let g: Vec<ModRef> = gameplay(mods).into_iter().map(ModRef::of).collect();
    if rule == MatchRule::Vanilla && !g.is_empty() {
        bail!("This match is vanilla only: turn off {}", names(&gameplay(mods)));
    }
    Ok(HostOffer { rule, gameplay: g, modset: modset_hash(mods, build, platform) })
}

fn names(ms: &[&ModRelease]) -> String { ms.iter().map(|m| m.name.as_str()).collect::<Vec<_>>().join(", ") }

#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct JoinPlan {
    pub rule: MatchRule,
    /// The mod set to start with for this game: the host's gameplay mods (none in a vanilla match) + my own
    /// client mods. Starting with this set is the "one click" fix.
    pub mods: Vec<ModRelease>,
    /// Host gameplay mods I do not have in this version (download from the signed index).
    pub download: Vec<ModRef>,
    /// My gameplay mods that are off for this game.
    pub turned_off: Vec<ModRef>,
    /// What to tell the player before the switch (vanilla match with my gameplay mods on).
    pub message: Option<String>,
}

/// `host` = the host's offer (as sent by the host). In a Mods match every gameplay entry must be in the signed
/// index for this build and platform; entries the index marks as client are ignored (a host cannot make a
/// client mod required). In a Vanilla match my gameplay mods are refused and the plan is my client mods only.
pub fn plan_join(host: &HostOffer, mine: &[ModRelease], index: &Index, build: &str, platform: &str) -> Result<JoinPlan> {
    if host.rule == MatchRule::Vanilla {
        if !host.gameplay.is_empty() { bail!("the host sent a vanilla match with gameplay mods: refused") }
        let off = gameplay(mine);
        let message = (!off.is_empty()).then(|| format!("This match is vanilla only: turn off {}", names(&off)));
        let turned_off = off.into_iter().map(ModRef::of).collect();
        let mods = mine.iter().filter(|m| m.scope == Scope::Client).cloned().collect();
        return Ok(JoinPlan { rule: host.rule, mods, download: vec![], turned_off, message });
    }
    let host = &host.gameplay;
    let mut mods = Vec::new();
    let mut download = Vec::new();
    for r in host {
        let Some(m) = index.mods.iter().find(|m| m.id == r.id && m.version == r.version) else {
            bail!("the host uses {} {}, which is not in the signed mod list", r.id, r.version);
        };
        if m.scope == Scope::Client { continue }
        if !m.supports(build, platform) { bail!("{} {} has no version for build {build} on this computer", m.name, m.version) }
        if !mine.iter().any(|x| x.id == m.id && x.version == m.version) { download.push(r.clone()) }
        mods.push(m.clone());
    }
    let turned_off = gameplay(mine).into_iter().filter(|m| !mods.iter().any(|x| x.id == m.id)).map(ModRef::of).collect();
    for m in mine.iter().filter(|m| m.scope == Scope::Client) {
        // my client mod, in the version for this build
        if let Some(c) = pick(index, &m.id, build, platform) { mods.push(c.clone()) }
    }
    Ok(JoinPlan { rule: MatchRule::Mods, mods, download, turned_off, message: None })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn idx() -> Index {
        let m = |id: &str, v: &str, scope: &str, sha: &str| serde_json::json!({
            "id": id, "name": id, "version": v, "kind": "code", "scope": scope, "game_builds": ["1"],
            "platforms": { "mac-arm64": { "files": [{ "name": format!("{id}.dylib"), "sha256": sha, "size": 1, "url": "https://x" }] } } });
        serde_json::from_value(serde_json::json!({ "schema": 1, "seq": 1, "mods": [
            m("multiplayer", "1.0.0", "gameplay", "aa"), m("hell", "1.0.0", "gameplay", "bb"), m("hell", "1.1.0", "gameplay", "bc"),
            m("weapon-skill-slot", "2.0.0", "client", "cc"), m("notch-fix", "1.0.0", "client", "dd") ] })).unwrap()
    }

    fn offer(gameplay: Vec<ModRef>) -> HostOffer { HostOffer { rule: MatchRule::Mods, gameplay, modset: String::new() } }

    #[test]
    fn vanilla_match_refuses_gameplay_mods_and_keeps_client_mods() {
        let i = idx();
        let mine = vec![get(&i, "multiplayer", "1.0.0"), get(&i, "hell", "1.0.0"), get(&i, "notch-fix", "1.0.0")];
        // host side: cannot host vanilla with a gameplay mod on
        let e = host_offer(MatchRule::Vanilla, &mine, "1", "mac-arm64").unwrap_err().to_string();
        assert_eq!(e, "This match is vanilla only: turn off multiplayer, hell");
        let host = host_offer(MatchRule::Vanilla, &[get(&i, "notch-fix", "1.0.0")], "1", "mac-arm64").unwrap();
        assert!(host.gameplay.is_empty());
        // joiner: message + one-click vanilla set (client mods only)
        let p = plan_join(&host, &mine, &i, "1", "mac-arm64").unwrap();
        assert_eq!(p.message.as_deref(), Some("This match is vanilla only: turn off multiplayer, hell"));
        assert_eq!(p.mods.iter().map(|m| m.key()).collect::<Vec<_>>(), vec!["notch-fix@1.0.0"]);
        assert_eq!(p.turned_off.len(), 2);
        assert_eq!(modset_hash(&p.mods, "1", "mac-arm64"), host.modset, "vanilla sets hash the same");
        // a joiner with only client mods gets no message
        assert_eq!(plan_join(&host, &[get(&i, "weapon-skill-slot", "2.0.0")], &i, "1", "mac-arm64").unwrap().message, None);
        // a vanilla offer that lists gameplay mods is refused
        let bad = HostOffer { rule: MatchRule::Vanilla, gameplay: vec![ModRef::of(&get(&i, "hell", "1.0.0"))], modset: String::new() };
        assert!(plan_join(&bad, &mine, &i, "1", "mac-arm64").is_err());
    }

    #[test]
    fn mods_match_offer_lists_host_gameplay_mods() {
        let i = idx();
        let host = host_offer(MatchRule::Mods, &[get(&i, "hell", "1.1.0"), get(&i, "notch-fix", "1.0.0")], "1", "mac-arm64").unwrap();
        assert_eq!(host.gameplay, vec![ModRef { id: "hell".into(), version: "1.1.0".into() }]);
        let p = plan_join(&host, &[], &i, "1", "mac-arm64").unwrap();
        assert_eq!(p.download, host.gameplay);
        assert_eq!(serde_json::to_string(&MatchRule::Vanilla).unwrap(), "\"vanilla\"");
    }

    fn get(i: &Index, id: &str, v: &str) -> ModRelease { i.mods.iter().find(|m| m.id == id && m.version == v).unwrap().clone() }

    #[test]
    fn client_mods_do_not_change_the_hash() {
        let i = idx();
        let a = vec![get(&i, "multiplayer", "1.0.0"), get(&i, "hell", "1.0.0")];
        let mut b = a.clone();
        b.push(get(&i, "notch-fix", "1.0.0"));
        b.reverse();
        assert_eq!(modset_hash(&a, "1", "mac-arm64"), modset_hash(&b, "1", "mac-arm64"));
        let c = vec![get(&i, "multiplayer", "1.0.0"), get(&i, "hell", "1.1.0")];
        assert_ne!(modset_hash(&a, "1", "mac-arm64"), modset_hash(&c, "1", "mac-arm64"), "a gameplay version change counts");
    }

    #[test]
    fn join_takes_host_gameplay_and_keeps_my_client_mods() {
        let i = idx();
        let host = vec![ModRef { id: "multiplayer".into(), version: "1.0.0".into() },
                        ModRef { id: "hell".into(), version: "1.1.0".into() },
                        ModRef { id: "weapon-skill-slot".into(), version: "2.0.0".into() }];
        let mine = vec![get(&i, "multiplayer", "1.0.0"), get(&i, "hell", "1.0.0"), get(&i, "notch-fix", "1.0.0")];
        let p = plan_join(&offer(host), &mine, &i, "1", "mac-arm64").unwrap();
        assert_eq!(p.download, vec![ModRef { id: "hell".into(), version: "1.1.0".into() }]);
        let ids: Vec<String> = p.mods.iter().map(|m| m.key()).collect();
        assert_eq!(ids, vec!["multiplayer@1.0.0", "hell@1.1.0", "notch-fix@1.0.0"], "host's client mod not forced on me");
        assert!(p.turned_off.is_empty());
        // equal hashes after the join
        let host_set: Vec<ModRelease> = vec![get(&i, "multiplayer", "1.0.0"), get(&i, "hell", "1.1.0"), get(&i, "weapon-skill-slot", "2.0.0")];
        assert_eq!(modset_hash(&p.mods, "1", "mac-arm64"), modset_hash(&host_set, "1", "mac-arm64"));
    }

    #[test]
    fn my_extra_gameplay_mod_is_off_for_the_game() {
        let i = idx();
        let host = vec![ModRef { id: "multiplayer".into(), version: "1.0.0".into() }];
        let mine = vec![get(&i, "multiplayer", "1.0.0"), get(&i, "hell", "1.0.0")];
        let p = plan_join(&offer(host), &mine, &i, "1", "mac-arm64").unwrap();
        assert_eq!(p.turned_off, vec![ModRef { id: "hell".into(), version: "1.0.0".into() }]);
    }

    #[test]
    fn unsigned_host_mod_is_refused() {
        let i = idx();
        let host = vec![ModRef { id: "evil".into(), version: "6.6.6".into() }];
        assert!(plan_join(&offer(host), &[], &i, "1", "mac-arm64").unwrap_err().to_string().contains("not in the signed"));
    }
}
