//! The builder's legality pass (builder.js validate(), sanitizeEq, normTags and the shelf), applied to a build in
//! place. What it does, in the builder's order: retired race -> Human; unknown background / traits dropped, traits
//! capped; portrait snapped into the race's pool; level clamped 1..max; a unit always has a background (first one
//! the race can roll); tree inscriptions routed to `insctrees`; inscription trees and inscriptions capped by the
//! level's slots; one soul stone; real acquired traits; elixirs 1..max; one level per need; items only in their own
//! slot, two-handers drop the off-hand, ammo only for a weapon that takes it; rolled bonuses normalized and clamped;
//! learned skills kept in order while level, prerequisites and each pool's points allow. What the prune removes is
//! SHELVED per tree (b.shelf) and learned again by itself when it fits again (owner decision 2026-07-28); item
//! rolls of a slot whose item changed are shelved the same way (b.shelfTags).
use crate::compute::{active_trees, bg_tree_set, compute, ind_cap, insc_cap, insc_slots, skill_cost, tree_is_weapon, tree_of_skill_any, tree_of_skill_for};
use crate::data::{dsb, to_i32, truthy, Item};
use crate::model::{Build, TagPick};
use indexmap::IndexMap;
use serde_json::{json, Map, Value};

/// the tree an inscription key grants (AFFECTER_<Tree>Inscription or <Tree minus Magic>Inscription), or None
pub fn insc_tree(k: &str) -> Option<String> {
    let d = dsb();
    for t in d.trees.keys() {
        if tree_insc_key(t).as_deref() == Some(k) {
            return Some(t.clone());
        }
    }
    None
}
/// treeInscKey: a tree's inscription key, if the game has one
pub fn tree_insc_key(t: &str) -> Option<String> {
    let d = dsb();
    let cands = [format!("AFFECTER_{t}Inscription"), format!("AFFECTER_{}Inscription", t.strip_suffix("Magic").unwrap_or(t))];
    cands.into_iter().find(|c| d.insc.iter().any(|x| &x.k == c))
}

fn pool_weight(pairs: &[(String, f64)], k: &str) -> f64 {
    let mut w = 0.0;
    for p in pairs {
        if p.0 == k {
            w = p.1
        }
    }
    w
}
/// a background the race can roll (or starts with in a scenario)
pub fn bg_avail(race: &crate::data::Race, k: &str) -> bool {
    pool_weight(&race.bg, k) > 0.0 || race.starter_bg.as_ref().map(|s| s.iter().any(|x| x == k)).unwrap_or(false)
}
/// an individual trait the race can roll
pub fn ind_avail(race: &crate::data::Race, k: &str) -> bool {
    pool_weight(&race.ind, k) > 0.0
}

fn ammo_fits(k: &str, need: Option<&str>) -> bool {
    let d = dsb();
    match (d.items.get(k), need) {
        (Some(a), Some(n)) if !n.is_empty() => a.slot == "Ammo" && format!("Ammo_{}", a.sub.as_deref().unwrap_or("undefined")) == n,
        _ => false,
    }
}

fn obj_of(b: &mut Build, key: &str) -> Map<String, Value> {
    match b.extra.get(key) {
        Some(Value::Object(m)) => m.clone(),
        _ => Map::new(),
    }
}

/// the slot write: stash the outgoing item's rolls, seat the new item, hand back that item's last rolls
pub fn set_slot_item(b: &mut Build, slot: &str, key: Option<&str>) {
    let cur = b.eq.get(slot).cloned().unwrap_or_default();
    if cur == key.unwrap_or("") {
        return;
    }
    shelf_tags_for(b, slot);
    match key.filter(|k| !k.is_empty()) {
        Some(k) => {
            b.eq.insert(slot.to_string(), k.to_string());
        }
        None => {
            b.eq.shift_remove(slot);
        }
    }
    unshelf_tags_for(b, slot);
}
fn shelf_tags_for(b: &mut Build, slot: &str) {
    let it = b.eq.get(slot).cloned().filter(|s| !s.is_empty());
    let rolls = b.tags.get(slot).cloned();
    if let (Some(it), Some(rolls)) = (it, rolls) {
        if !rolls.is_empty() {
            let mut ts = obj_of(b, "shelfTags");
            let mut rec = match ts.get(slot) {
                Some(Value::Object(m)) => m.clone(),
                _ => Map::new(),
            };
            rec.insert(it, serde_json::to_value(&rolls).unwrap_or(Value::Null));
            ts.insert(slot.to_string(), Value::Object(rec));
            b.extra.insert("shelfTags".into(), Value::Object(ts));
        }
    }
    b.tags.shift_remove(slot);
}
fn unshelf_tags_for(b: &mut Build, slot: &str) {
    let Some(it) = b.eq.get(slot).cloned().filter(|s| !s.is_empty()) else { return };
    let mut ts = obj_of(b, "shelfTags");
    b.extra.insert("shelfTags".into(), Value::Object(ts.clone()));
    let Some(Value::Object(rec)) = ts.get(slot).cloned() else { return };
    let Some(rolls) = rec.get(&it).filter(|v| truthy(Some(v))).cloned() else { return };
    let picks: Vec<Option<TagPick>> = serde_json::from_value(rolls).unwrap_or_default();
    b.tags.insert(slot.to_string(), picks);
    let mut rec = rec;
    rec.remove(&it);
    if rec.is_empty() {
        ts.remove(slot);
    } else {
        ts.insert(slot.to_string(), Value::Object(rec));
    }
    b.extra.insert("shelfTags".into(), Value::Object(ts));
}

/// sanitizeEq: a two-hander drops the off-hand; ammo only when the weapon in hand takes it
pub fn sanitize_eq(b: &mut Build) {
    let d = dsb();
    let prim = b.eq.get("Primary").and_then(|k| d.items.get(k));
    let two = prim.map(|p| truthy(p.th.as_ref())).unwrap_or(false);
    let need = prim.and_then(|p| p.ammo.clone());
    if two && b.eq.get("Secondary").map(|s| !s.is_empty()).unwrap_or(false) {
        set_slot_item(b, "Secondary", None);
    }
    if let Some(a) = b.eq.get("Ammo").cloned().filter(|s| !s.is_empty()) {
        if !ammo_fits(&a, need.as_deref()) {
            set_slot_item(b, "Ammo", None);
        }
    }
}

/// normTags: keep at most the item's roll count, each roll {i, v} with v clamped into its range;
/// a quick-set default (d: 1) follows the roll setting
pub fn norm_tags(it: &Item, arr: &[Option<TagPick>], q: f64) -> Vec<Option<TagPick>> {
    let tags = it.tags.as_deref().unwrap_or(&[]);
    let n = match it.tn {
        Some(t) if t != 0.0 && !t.is_nan() => t.max(0.0) as usize,
        _ => 1,
    };
    arr.iter()
        .take(n)
        .map(|x| {
            let x = x.as_ref()?;
            let idx = x.idx()?;
            if !(idx >= 0.0 && idx < tags.len() as f64 && idx.fract() == 0.0) {
                return None;
            }
            let tg = &tags[idx as usize];
            if let TagPick::Obj { d: Some(dv), .. } = x {
                if truthy(Some(dv)) {
                    return Some(TagPick::Obj { i: Some(idx), v: Some(tg.1 + (tg.2 - tg.1) * q), d: Some(json!(1)) });
                }
            }
            let v = match x.val() {
                Some(v) if x.is_obj() => v,
                _ => tg.1 + (tg.2 - tg.1) * q,
            };
            let v = tg.1.max(tg.2.min(v));
            Some(TagPick::Obj { i: Some(idx), v: Some(v), d: None })
        })
        .collect()
}

fn shelf_of(b: &Build) -> IndexMap<String, Value> {
    match b.extra.get("shelf") {
        Some(Value::Object(m)) => m.iter().map(|(k, v)| (k.clone(), v.clone())).collect(),
        _ => IndexMap::new(),
    }
}
fn put_shelf(b: &mut Build, sh: &IndexMap<String, Value>) {
    b.extra.insert("shelf".into(), Value::Object(sh.iter().map(|(k, v)| (k.clone(), v.clone())).collect()));
}
fn shelf_put(sh: &mut IndexMap<String, Value>, k: &str) {
    if !dsb().skills.contains_key(k) {
        return;
    }
    let Some(t) = tree_of_skill_any(k) else { return };
    let e = sh.entry(t).or_insert_with(|| json!([]));
    if !e.is_array() {
        *e = json!([]);
    }
    let a = e.as_array_mut().unwrap();
    if !a.iter().any(|x| x.as_str() == Some(k)) {
        a.push(json!(k));
    }
}
fn shelf_drop(sh: &mut IndexMap<String, Value>, k: &str) {
    let keys: Vec<String> = sh.keys().cloned().collect();
    for t in keys {
        let v = sh.get_mut(&t).unwrap();
        if let Some(a) = v.as_array_mut() {
            if let Some(i) = a.iter().position(|x| x.as_str() == Some(k)) {
                a.remove(i);
            }
            if a.is_empty() {
                sh.shift_remove(&t);
            }
        } else if !truthy(Some(v)) || v.get("length").is_none() {
            sh.shift_remove(&t);
        }
    }
}

/// validate(): the builder's rules, in place
pub fn validate(b: &mut Build) {
    let d = dsb();
    if d.retired.races.contains_key(b.race.as_deref().unwrap_or("")) {
        b.race = Some("AFFECTER_Human".into());
    }
    let race = d.races.iter().find(|r| Some(&r.k) == b.race.as_ref());
    if let Some(race) = race {
        if b.bg.as_ref().map(|k| !k.is_empty() && !d.bg.contains_key(k)).unwrap_or(false) {
            b.bg = None;
        }
        b.inds = b.inds.iter().filter(|k| d.ind.contains_key(*k)).take(ind_cap()).cloned().collect();
        let pool = d.portraits.get(&race.k).cloned().unwrap_or_default();
        if let Some(p) = b.portrait.as_ref().filter(|p| !p.is_empty()) {
            if !pool.contains(p) {
                b.portrait = pool.first().cloned();
            }
        }
    }
    b.lvl = Some((to_i32(b.lvl.unwrap_or(0.0)) as i64).min(d.consts.max_level).max(1) as f64);
    if let Some(race) = race {
        if b.bg.as_ref().map(|s| s.is_empty()).unwrap_or(true) {
            let mut first = race.bg.iter().find(|p| bg_avail(race, &p.0) && d.bg.contains_key(&p.0)).map(|p| p.0.clone());
            if first.is_none() {
                first = race.bg.iter().find(|p| d.bg.contains_key(&p.0)).map(|p| p.0.clone());
            }
            b.bg = first;
        }
    }
    b.extra.shift_remove("main");
    if let Some(subs) = b.extra.shift_remove("subs") {
        if let Some(a) = subs.as_array() {
            b.insctrees.extend(a.iter().filter_map(|x| x.as_str().map(String::from)));
        }
    }
    let mut keep = vec![];
    for k in std::mem::take(&mut b.insc) {
        match insc_tree(&k) {
            Some(t) => {
                if !b.insctrees.contains(&t) {
                    b.insctrees.push(t)
                }
            }
            None => keep.push(k),
        }
    }
    b.insc = keep;
    let bgset = bg_tree_set(b);
    let mut seen: Vec<String> = vec![];
    b.insctrees.retain(|t| {
        if !d.trees.contains_key(t) || bgset.contains(t) || seen.contains(t) {
            return false;
        }
        seen.push(t.clone());
        true
    });
    let n = insc_slots(b).max(0.0) as usize;
    b.insctrees.truncate(n);
    let n = insc_cap(b).max(0.0) as usize;
    b.insc.truncate(n);
    let mut souls: Vec<String> = vec![];
    for k in &b.souls {
        if d.souls.contains_key(k) && !souls.contains(k) {
            souls.push(k.clone())
        }
    }
    souls.truncate(1);
    b.souls = souls;
    let mut acq: Vec<String> = vec![];
    for k in &b.acq_inds {
        if d.acq_trait(k).is_some() && !acq.contains(k) {
            acq.push(k.clone())
        }
    }
    b.acq_inds = acq;
    let mut ex = IndexMap::new();
    for m in &d.majors {
        let n = b.elix_uses(m);
        if n != 0 && d.elix_row(m).is_some() {
            ex.insert(m.clone(), json!(n));
        }
    }
    b.elix = ex;
    b.cond = b.cond_on().iter().map(|o| o.k.clone()).collect();
    b.eq.retain(|slot, k| d.items.get(k).map(|it| &it.slot == slot).unwrap_or(false));
    sanitize_eq(b);
    let q = b.roll_q();
    let slots: Vec<String> = b.tags.keys().cloned().collect();
    for slot in slots {
        let it = b.eq.get(&slot).and_then(|k| d.items.get(k)).filter(|it| it.tags.is_some());
        match it {
            None => {
                b.tags.shift_remove(&slot);
            }
            Some(it) => {
                let n = norm_tags(it, &b.tags[&slot], q);
                b.tags.insert(slot, n);
            }
        }
    }
    // the shelved rolls get the same normalization; a stash for the item now in the slot is dropped
    let mut tsh = obj_of(b, "shelfTags");
    let tslots: Vec<String> = tsh.keys().cloned().collect();
    for slot in tslots {
        let Some(Value::Object(rec)) = tsh.get(&slot).cloned() else {
            tsh.remove(&slot);
            continue;
        };
        let mut rec2 = Map::new();
        for (ik, rolls) in rec {
            let Some(it) = d.items.get(&ik) else { continue };
            if it.slot != slot || it.tags.is_none() || b.eq.get(&slot) == Some(&ik) {
                continue;
            }
            let arr: Vec<Option<TagPick>> = match &rolls {
                Value::Array(_) => serde_json::from_value(rolls.clone()).unwrap_or_default(),
                _ => vec![],
            };
            rec2.insert(ik, serde_json::to_value(norm_tags(it, &arr, q)).unwrap_or(Value::Null));
        }
        if rec2.is_empty() {
            tsh.remove(&slot);
        } else {
            tsh.insert(slot, Value::Object(rec2));
        }
    }
    b.extra.insert("shelfTags".into(), Value::Object(tsh));
    // learned skills, in order, within level, prerequisites and each pool's points
    let c = compute(b);
    let mut main_bud = crate::compute::main_sp_budget(b);
    let mut sub_bud = crate::compute::sub_sp_budget(b);
    let active = active_trees(b);
    let lvl = b.level();
    let mut kept: Vec<String> = vec![];
    let mut lost: Vec<String> = vec![];
    for k in b.learned.clone() {
        let Some(sk) = d.skills.get(&k) else { continue };
        let Some(tree) = tree_of_skill_for(&k, b).filter(|t| active.contains(t)) else {
            lost.push(k);
            continue;
        };
        if sk.lv > lvl {
            lost.push(k);
            continue;
        }
        if sk.pre.iter().any(|p| !kept.contains(p)) {
            lost.push(k);
            continue;
        }
        let cost = skill_cost(&k, &tree, &c.st);
        if tree_is_weapon(&tree) {
            if cost > main_bud {
                lost.push(k);
                continue;
            }
            main_bud -= cost;
        } else {
            if sk.sp > sub_bud {
                lost.push(k);
                continue;
            }
            sub_bud -= sk.sp;
        }
        kept.push(k);
    }
    b.learned = kept.clone();
    let mut sh = shelf_of(b);
    for k in &lost {
        shelf_put(&mut sh, k)
    }
    for k in &kept {
        shelf_drop(&mut sh, k)
    }
    let ts: Vec<String> = sh.keys().cloned().collect();
    for t in ts {
        let v = sh[&t].clone();
        let Some(a) = v.as_array().filter(|_| d.trees.contains_key(&t)) else {
            sh.shift_remove(&t);
            continue;
        };
        let mut lst: Vec<Value> = vec![];
        for x in a {
            if let Some(s) = x.as_str() {
                if d.skills.contains_key(s) && !lst.iter().any(|y| y.as_str() == Some(s)) {
                    lst.push(x.clone())
                }
            }
        }
        if lst.is_empty() {
            sh.shift_remove(&t);
        } else {
            sh.insert(t, Value::Array(lst));
        }
    }
    // restore: re-learn every shelved skill that fits again, to a fixed point
    let lim: usize = sh.values().map(|v| v.as_array().map(|a| a.len()).unwrap_or(0)).sum();
    let mut have: Vec<String> = b.learned.clone();
    for _ in 0..lim {
        let mut moved = false;
        for t in active_trees(b) {
            let Some(lst) = sh.get(&t).and_then(Value::as_array).cloned() else { continue };
            if lst.is_empty() {
                continue;
            }
            let mut still: Vec<Value> = vec![];
            for kv in lst {
                let k = kv.as_str().unwrap_or("").to_string();
                let sk = d.skills.get(&k);
                let mut ok = sk.is_some() && !have.contains(&k) && sk.map(|s| s.lv <= lvl).unwrap_or(false);
                if ok && sk.unwrap().pre.iter().any(|p| !have.contains(p)) {
                    ok = false;
                }
                if ok {
                    let sk = sk.unwrap();
                    let cost = skill_cost(&k, &t, &c.st);
                    if tree_is_weapon(&t) {
                        if cost > main_bud {
                            ok = false
                        } else {
                            main_bud -= cost
                        }
                    } else if sk.sp > sub_bud {
                        ok = false
                    } else {
                        sub_bud -= sk.sp
                    }
                }
                if ok {
                    b.learned.push(k.clone());
                    have.push(k);
                    moved = true;
                } else if sk.is_some() && !have.contains(&k) {
                    still.push(kv);
                }
            }
            if still.is_empty() {
                sh.shift_remove(&t);
            } else {
                sh.insert(t, Value::Array(still));
            }
        }
        if !moved {
            break;
        }
    }
    put_shelf(b, &sh);

}
