//! The stat sheet: a line-by-line port of the wiki builder's pure core (site/builder/builder.js: computeBuild,
//! hitBonusOf, the tree / skill point / stacking-passive helpers). Same operations in the same order on f64, so
//! the numbers are the builder's to the last bit (tests/sheet_vs_builder.rs runs builder.js in node and compares).
//! Owner rule (CORE_TRUTHS.md, 2026-10-14): conditional and acquired passives fold at their MAXIMUM on a hand-built
//! unit, on purpose. This is the builder's sheet, not an at-rest game unit.
use crate::data::{dsb, js_num, truthy, Adj, PassiveGrant};
use crate::model::Build;
use indexmap::IndexMap;
use serde::Serialize;
use serde_json::{json, Value};

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Row {
    pub label: String,
    pub v: f64,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Sheet {
    /// the six attributes as the sheet shows them
    pub majors: IndexMap<String, f64>,
    /// attribute before the live major % modifiers (for a save writer)
    pub majors_raw: IndexMap<String, f64>,
    /// flat major contributions folded into majors_raw
    pub major_flat: IndexMap<String, f64>,
    /// every stat of the sheet (StatusValueTable order), plus typed overrides
    pub st: IndexMap<String, f64>,
    /// per-stat breakdown rows (Base, each attribute, each source, each % as its resolved delta)
    pub bd: IndexMap<String, Vec<Row>>,
    /// adjust targets with no sheet row: [stat, value, is_percent]
    pub leftover: Vec<(String, f64, u8)>,
    pub tal_bump: i64,
    pub manual_keys: Vec<String>,
    pub passive_crit: f64,
    /// summed % modifiers per stat
    pub pct: IndexMap<String, f64>,
    /// what changes each hit beyond P.Atk / M.Atk (not counted in any stat)
    pub hit: HitBonus,
}

#[derive(Debug, Clone, Serialize, Default)]
pub struct HitBonus {
    pub pa: f64,
    pub ma: f64,
    pub dmel: f64,
    pub pdd: f64,
    pub mdd: f64,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub dphys: Option<f64>,
    pub rows: Vec<Value>,
}

fn add(m: &mut IndexMap<String, f64>, k: &str, v: f64) {
    *m.entry(k.to_string()).or_insert(0.0) += v;
}
fn get(m: &IndexMap<String, f64>, k: &str) -> f64 {
    m.get(k).copied().unwrap_or(0.0)
}

struct Acc {
    flat: IndexMap<String, f64>,
    pct: IndexMap<String, f64>,
    fin: IndexMap<String, f64>,
    src_flat: IndexMap<String, Vec<Row>>,
    src_pct: IndexMap<String, Vec<Row>>,
}
impl Acc {
    fn rec(m: &mut IndexMap<String, Vec<Row>>, k: &str, label: Option<&str>, v: f64) {
        let Some(l) = label.filter(|l| !l.is_empty()) else { return };
        m.entry(k.to_string()).or_default().push(Row { label: l.to_string(), v });
    }
    fn apply(&mut self, adj: &[(String, f64)], label: Option<&str>) {
        for (s, v) in adj {
            let v = *v;
            if let Some(b1) = s.strip_suffix("PerModifier") {
                add(&mut self.pct, b1, v);
                Self::rec(&mut self.src_pct, b1, label, v);
            } else if let Some(b1) = s.strip_suffix("FinalMultiflier") {
                add(&mut self.fin, b1, v);
            } else {
                add(&mut self.flat, s, v);
                Self::rec(&mut self.src_flat, s, label, v);
            }
        }
    }
}

/// Math.round as the game does it: nearest whole number, exact halves to the even one
pub fn round_half_even(x: f64) -> f64 {
    let f = x.floor();
    let d = x - f;
    if (d - 0.5).abs() < 1e-9 {
        if f % 2.0 == 0.0 {
            f
        } else {
            f + 1.0
        }
    } else {
        crate::data::js_round(x)
    }
}

/// average gain per level-up for one major (whole-number roll in the tier range x offering, half to even)
pub fn growth_per_level(rg: (f64, f64), off: f64, q: f64) -> f64 {
    if !dsb().consts.growth_int {
        return (rg.0 + (rg.1 - rg.0) * q) * off;
    }
    let ks: Vec<f64> = if q == 0.0 {
        vec![rg.0]
    } else if q == 1.0 {
        vec![rg.1]
    } else {
        let mut v = vec![];
        let mut k = rg.0;
        while k <= rg.1 {
            v.push(k);
            k += 1.0;
        }
        v
    };
    let n = ks.len() as f64;
    ks.iter().fold(0.0, |a, k| a + round_half_even(k * off)) / n
}

/// the generation bake: (int)((roll / 100 + 1) x base) in float32, truncated toward zero
pub fn gen_bake(b0: f64, roll_pct: f64) -> f64 {
    let r = roll_pct as f32;
    let x = (r / 100f32 + 1f32) * (b0 as f32);
    (x as f64).trunc()
}

pub fn tree_is_weapon(t: &str) -> bool {
    dsb().trees.get(t).map(|x| x.kind == "main").unwrap_or(false)
}
pub fn bg_weapon_trees(b: &Build) -> Vec<String> {
    let d = dsb();
    match b.bg.as_ref().and_then(|k| d.bg.get(k)) {
        Some(bg) => bg.mains.iter().filter(|t| d.trees.contains_key(*t)).cloned().collect(),
        None => vec![],
    }
}
pub fn bg_sub_trees(b: &Build) -> Vec<String> {
    let d = dsb();
    match b.bg.as_ref().and_then(|k| d.bg.get(k)) {
        Some(bg) => bg.subs.iter().map(|p| p.0.clone()).filter(|t| d.trees.contains_key(t)).collect(),
        None => vec![],
    }
}
/// the background's granted trees (weapon, then auxiliary), no duplicates, first seen order
pub fn bg_tree_set(b: &Build) -> Vec<String> {
    let mut out: Vec<String> = vec![];
    for t in bg_weapon_trees(b).into_iter().chain(bg_sub_trees(b)) {
        if !out.contains(&t) {
            out.push(t);
        }
    }
    out
}
/// every tree the unit has: background naturals + inscription trees
pub fn active_trees(b: &Build) -> Vec<String> {
    let d = dsb();
    let set = bg_tree_set(b);
    let mut out = set.clone();
    for t in &b.insctrees {
        if d.trees.contains_key(t) && !set.contains(t) {
            out.push(t.clone());
        }
    }
    out
}
pub fn tree_of_skill_for(k: &str, b: &Build) -> Option<String> {
    let d = dsb();
    for t in active_trees(b) {
        let Some(tr) = d.trees.get(&t) else { continue };
        for row in &tr.rows {
            for c in 0..3 {
                if row.get(c).and_then(|x| x.as_deref()) == Some(k) {
                    return Some(t);
                }
            }
        }
    }
    None
}
/// the tree a skill sits in among ALL trees (active or not)
pub fn tree_of_skill_any(k: &str) -> Option<String> {
    for (t, tr) in &dsb().trees {
        if tr.rows.iter().any(|r| r.iter().any(|x| x.as_deref() == Some(k))) {
            return Some(t.clone());
        }
    }
    None
}
pub fn insc_slots(b: &Build) -> f64 {
    ((b.level() - 1.0) * dsb().consts.insc_per_level).max(0.0)
}
pub fn insc_cap(b: &Build) -> f64 {
    (insc_slots(b) - b.insctrees.len() as f64).max(0.0)
}
pub fn insc_free(b: &Build) -> f64 {
    (insc_cap(b) - b.insc.len() as f64).max(0.0)
}
pub fn ind_cap() -> usize {
    let n = dsb().consts.ind_max;
    if n > 0.0 {
        n as usize
    } else {
        4
    }
}
fn extra_tree_sp(b: &Build) -> (f64, f64) {
    let d = dsb();
    let (mut m, mut s) = (0.0, 0.0);
    let g = d.consts.sub_tree_insc_sp;
    for t in &b.insctrees {
        let Some(tr) = d.trees.get(t) else { continue };
        if tr.kind == "main" {
            m += g
        } else {
            s += g
        }
    }
    (m, s)
}
pub fn main_sp_budget(b: &Build) -> f64 {
    let c = &dsb().consts;
    c.main_sp0 + c.main_spi * (b.level() - 1.0) + extra_tree_sp(b).0
}
pub fn sub_sp_budget(b: &Build) -> f64 {
    let c = &dsb().consts;
    c.sub_sp0 + c.sub_spi * (b.level() - 1.0) + extra_tree_sp(b).1
}
/// SP cost: table SP - Math.Round(aptitude) on a weapon tree with an aptitude stat; no floor
pub fn skill_cost(skill: &str, tree: &str, st: &IndexMap<String, f64>) -> f64 {
    let d = dsb();
    let Some(sk) = d.skills.get(skill) else { return 0.0 };
    let base = sk.sp;
    if tree_is_weapon(tree) {
        if let Some(apt) = d.apt.get(tree) {
            let c = base - round_half_even(get(st, apt));
            return match d.consts.sp_cost_min {
                None => c,
                Some(lo) => lo.max(c),
            };
        }
    }
    base
}

/// unitSkillSets: what the unit knows, grouped as the game's skill component stores it
#[derive(Debug, Clone, Serialize, Default)]
pub struct SkillSets {
    pub basics: Vec<String>,
    pub actives: Vec<String>,
    pub passives: Vec<String>,
    pub extra: Vec<String>,
}
pub fn unit_skill_sets(b: &Build, donor_basics: Option<&[String]>) -> SkillSets {
    let d = dsb();
    let mut s = SkillSets::default();
    if let Some(db) = donor_basics.filter(|x| !x.is_empty()) {
        s.basics.push(db[0].clone());
    }
    for k in &b.learned {
        let Some(sk) = d.skills.get(k) else { continue };
        if sk.ty == "Passive" {
            if !s.passives.contains(k) {
                s.passives.push(k.clone())
            }
        } else if !s.actives.contains(k) {
            s.actives.push(k.clone())
        }
    }
    for slot in &d.slots {
        let Some(it) = b.eq.get(slot).and_then(|k| d.items.get(k)) else { continue };
        if let Some(bsk) = it.bsk.as_ref().filter(|x| !x.is_empty()) {
            if !s.basics.contains(bsk) {
                s.basics.push(bsk.clone())
            }
        }
        for sk in it.defskills.iter().flatten() {
            if Some(sk) == it.bsk.as_ref() || s.basics.contains(sk) {
                continue;
            }
            if s.actives.contains(sk) || s.passives.contains(sk) {
                continue;
            }
            if d.skills.get(sk).map(|x| x.ty == "Passive").unwrap_or(false) {
                s.passives.push(sk.clone())
            } else {
                s.actives.push(sk.clone())
            }
            if !s.extra.contains(sk) {
                s.extra.push(sk.clone())
            }
        }
    }
    s
}
/// shortcut-bar slots: the shipped templates' QuickSlots rows
pub fn quick_slot_count() -> usize {
    let mut n = 0;
    if let Some(t) = dsb().savegen.get("tpl").and_then(Value::as_object) {
        for v in t.values() {
            let q = v.get("qs").and_then(Value::as_f64).unwrap_or(0.0);
            if q > n as f64 {
                n = q as usize
            }
        }
    }
    if n == 0 {
        4
    } else {
        n
    }
}
pub fn bar_only(k: &str) -> bool {
    dsb().bar_only_skills.iter().any(|x| x == k)
}
/// the shortcut bar a build arrives with: its own pick (b.quick) when valid, else every active in learn order
pub fn quick_for(b: &Build) -> Vec<String> {
    let av: Vec<String> = unit_skill_sets(b, None).actives.into_iter().filter(|k| !bar_only(k)).collect();
    let cap = quick_slot_count();
    let pick: Option<Vec<String>> = b.extra.get("quick").and_then(Value::as_array).map(|a| {
        a.iter().filter_map(Value::as_str).filter(|k| av.iter().any(|x| x == k)).map(String::from).collect()
    });
    let out = match pick {
        Some(p) if !p.is_empty() => p,
        _ => av,
    };
    out.into_iter().take(cap).collect()
}

pub struct StackPassive {
    pub k: String,
    pub pg: &'static PassiveGrant,
    pub max: usize,
    pub learned: bool,
}
/// milestone passives (thr + per + aff) of a unit that has the skill's tree
pub fn stack_passives(b: &Build) -> Vec<StackPassive> {
    let d = dsb();
    let mut out = vec![];
    for (k, pg) in &d.passive_grants {
        let thr_ok = pg.thr.as_ref().map(|t| !t.is_empty()).unwrap_or(false);
        let aff_ok = pg.aff.as_ref().map(|a| !a.is_empty()).unwrap_or(false);
        let learned = b.learned.contains(k);
        if aff_ok && thr_ok && tree_of_skill_for(k, b).is_some() && !(d.consts.banked_needs_skill && !learned) {
            out.push(StackPassive { k: k.clone(), pg, max: pg.thr.as_ref().unwrap().len(), learned });
        }
    }
    out
}
pub fn stack_lv_of(pg: &PassiveGrant, n: f64) -> usize {
    pg.thr.iter().flatten().filter(|t| n >= **t).count()
}
/// the raw affecter Stack the unit carries for one stacking passive (stackCountFor; no remembered default)
pub fn stack_count_for(b: &Build, k: &str) -> f64 {
    let d = dsb();
    let Some(pg) = d.passive_grants.get(k) else { return 0.0 };
    let Some(thr) = pg.thr.as_ref().filter(|t| !t.is_empty()) else { return 0.0 };
    let learned = b.learned.iter().any(|x| x == k);
    if let Some(Some(pick)) = b.stack_lv.as_ref().and_then(|m| m.get(k)) {
        let mut lv = crate::data::js_round(js_num(pick));
        if !(lv > 0.0) {
            return 0.0;
        }
        if lv > thr.len() as f64 {
            lv = thr.len() as f64
        }
        return thr[lv as usize - 1];
    }
    if let Some(Some(stk)) = b.passive_stacks.as_ref().and_then(|m| m.get(k)) {
        return *stk;
    }
    if b.is_imported() {
        return 0.0;
    }
    if !learned {
        return 0.0;
    }
    thr[thr.len() - 1]
}
pub fn stack_lv_for(b: &Build, k: &str) -> usize {
    match dsb().passive_grants.get(k) {
        Some(pg) => stack_lv_of(pg, stack_count_for(b, k)),
        None => 0,
    }
}

/// gearFxOf: the code-side gear effects of a build (one per kind)
fn gear_fx_of(b: &Build) -> Vec<(String, String, Option<String>, (String, f64))> {
    let d = dsb();
    let mut out = vec![];
    let mut seen: Vec<String> = vec![];
    let mut push = |k: &str, n: &str, ic: Option<&String>, g: Option<&(String, f64)>| {
        if let Some(g) = g {
            if !seen.contains(&g.0) {
                seen.push(g.0.clone());
                out.push((k.to_string(), n.to_string(), ic.cloned(), g.clone()));
            }
        }
    };
    for slot in &d.slots {
        if let Some(k) = b.eq.get(slot).filter(|k| !k.is_empty()) {
            if let Some(it) = d.items.get(k) {
                push(k, &it.n, it.ic.as_ref(), it.gfx.as_ref());
            }
        }
    }
    for k in b.ind_list() {
        if let Some(t) = d.ind.get(k) {
            push(k, &t.n, t.ic.as_ref(), t.gfx.as_ref());
        }
    }
    out
}

/// wfillOf: the carried share of Max Weight, percent (default 100)
pub fn wfill_of(b: &Build) -> f64 {
    let Some(w) = b.wfill.as_ref() else { return 100.0 };
    if w.is_null() || w.as_str() == Some("") {
        return 100.0;
    }
    let p = js_num(w);
    if (0.0..=100.0).contains(&p) {
        p
    } else {
        100.0
    }
}

fn sgn(v: f64) -> i32 {
    if v > 0.0 {
        1
    } else {
        -1
    }
}

fn hit_bonus_of(b: &Build, st: &IndexMap<String, f64>, bd: &IndexMap<String, Vec<Row>>) -> HitBonus {
    let d = dsb();
    let mut o = HitBonus::default();
    for (k, side) in [("DamageDealtBonus", "both"), ("PhysicalDamageDealtBonus", "P"), ("MagicDamageDealtBonus", "M")] {
        let v = get(st, k);
        if v.abs() < 0.05 {
            continue;
        }
        if side != "M" {
            o.pdd += v
        }
        if side != "P" {
            o.mdd += v
        }
        let src: Vec<&Row> = bd.get(k).map(|r| r.iter().filter(|r| r.v.abs() >= 0.05).collect()).unwrap_or_default();
        o.rows.push(json!({"kind":"stat","stat":k,"side":side,"sgn":sgn(v),"v":v,"src":src}));
    }
    if let Some(fdm) = st.get("FinalDamageMultiplier").copied() {
        if (fdm - 1.0).abs() > 0.0005 {
            let src = bd.get("FinalDamageMultiplier").cloned().unwrap_or_default();
            o.rows.push(json!({"kind":"fdm","stat":"FinalDamageMultiplier","side":"both","sgn":if fdm>1.0 {1} else {-1},"v":fdm,"src":src}));
        }
    }
    for k in &b.insc {
        let Some(r) = d.insc_row(k) else { continue };
        let Some((kind, v)) = r.fx.as_ref() else { continue };
        let v = *v;
        let mut row = json!({"kind":kind,"k":k,"n":r.n,"ic":r.ic,"pct":v,"side":"both","sgn":1,"cond":1,"ct":r.ct});
        if kind == "pahp" || kind == "mahp" {
            let hp = get(st, "MaxHealthTotal");
            let g = hp * v / 100.0;
            if kind == "pahp" {
                o.pa += g
            } else {
                o.ma += g
            }
            row["side"] = json!(if kind == "pahp" { "P" } else { "M" });
            row["cond"] = json!(0);
            row["v"] = json!(g);
            row["base"] = json!(hp);
        } else if kind == "ddwt" {
            let w = get(st, "MaxWeight") * wfill_of(b) / 100.0;
            let dd = w * v / 100.0;
            o.dmel += dd;
            row["side"] = json!("P");
            row["v"] = json!(dd);
            row["base"] = json!(w);
            row["fill"] = json!(wfill_of(b));
            row["mw"] = json!(get(st, "MaxWeight"));
        }
        o.rows.push(row);
    }
    for k in &b.souls {
        let Some(r) = d.souls.get(k) else { continue };
        let Some(f) = r.fx.as_ref() else { continue };
        let kind = f.first().and_then(Value::as_str).unwrap_or("").to_string();
        let mut row = json!({"kind":format!("soul_{kind}"),"k":k,"n":r.n,"ic":r.ic,"fx":f,"side":"both","sgn":1,"cond":1});
        if kind == "ddwtp" {
            let w = get(st, "MaxWeight") * wfill_of(b) / 100.0;
            let f1 = f.get(1).map(js_num).unwrap_or(f64::NAN);
            let v = w * f1 / 100.0;
            row["side"] = json!("P");
            row["cond"] = json!(0);
            row["v"] = json!(v);
            row["base"] = json!(w);
            row["fill"] = json!(wfill_of(b));
            row["mw"] = json!(get(st, "MaxWeight"));
            o.dphys = Some(o.dphys.unwrap_or(0.0) + v);
        } else if kind == "malow" || kind == "fract" {
            row["side"] = json!("M");
        }
        o.rows.push(row);
    }
    for (k, n, ic, f) in gear_fx_of(b) {
        if f.0 == "ddmood" {
            let md = get(st, "Mood");
            let v = f.1 * md;
            o.pdd += v;
            o.mdd += v;
            o.rows.push(json!({"kind":"gear_ddmood","k":k,"n":n,"ic":ic,"side":"both","sgn":if v<0.0 {-1} else {1},"cond":1,"v":v,"mood":md,"per":f.1}));
        } else if f.0 == "cclost" {
            o.rows.push(json!({"kind":"gear_cclost","k":k,"n":n,"ic":ic,"side":"both","sgn":1,"cond":1,"max":f.1}));
        }
    }
    let ss = unit_skill_sets(b, None);
    for k in ss.actives.iter().chain(ss.passives.iter()) {
        let Some(h) = d.hit_skills.get(k) else { continue };
        for (eff, v) in &h.eff {
            let side = match eff.as_str() {
                "pa" | "pa1h" | "pdd" => "P",
                "ma" | "mdd" => "M",
                "dd" | "ign" => "both",
                _ => continue,
            };
            o.rows.push(json!({"kind":"skill","sk":k,"eff":eff,"v":v,"max":h.max.unwrap_or(0.0),"act":h.act,"side":side,"sgn":if *v>=0.0 {1} else {-1},"cond":1}));
        }
    }
    o
}

fn label(n: &Option<String>) -> Option<&str> {
    n.as_deref()
}

/// computeBuild
pub fn compute(b: &Build) -> Sheet {
    let d = dsb();
    let maj = &d.majors;
    let q = b.roll_q();
    let off = {
        let k = b.offer.as_deref().filter(|s| !s.is_empty()).unwrap_or("Least");
        match d.offer.get(k).copied() {
            Some(v) if v != 0.0 && !v.is_nan() => v,
            _ => 1.0,
        }
    };
    let lvl = b.level();
    let mut a = Acc {
        flat: IndexMap::new(),
        pct: IndexMap::new(),
        fin: IndexMap::new(),
        src_flat: IndexMap::new(),
        src_pct: IndexMap::new(),
    };
    let race = b.race.as_deref().and_then(|k| d.race(k));
    let bg = b.bg.as_deref().filter(|s| !s.is_empty()).and_then(|k| d.bg.get(k));
    if let Some(r) = race {
        a.apply(&r.adj, Some(&r.n));
    }
    if let Some(g) = bg {
        a.apply(&g.adj, Some(&g.n));
    }
    for k in b.ind_list() {
        if let Some(t) = d.ind.get(k) {
            a.apply(&t.adj, Some(&t.n));
        }
    }
    for k in &b.souls {
        if let Some(s) = d.souls.get(k) {
            a.apply(&s.adj, Some(&s.n));
        }
    }
    for k in &b.acq_inds {
        if let Some(t) = d.acq_trait(k) {
            a.apply(&t.adj, Some(&t.n));
        }
    }
    for o in b.cond_on() {
        a.apply(&o.adj, Some(&o.n));
    }
    let mut passive_crit = 0.0;
    for k in &b.learned {
        let Some(pg) = d.passive_grants.get(k) else { continue };
        if !truthy(pg.cond.as_ref()) {
            a.apply(&pg.adj, label(&pg.n));
            continue;
        }
        let has_thr = pg.thr.as_ref().map(|t| !t.is_empty()).unwrap_or(false);
        if pg.aff.as_ref().map(|x| !x.is_empty()).unwrap_or(false) && has_thr {
            continue;
        }
        let cur: Adj = if b.is_imported() {
            let Some(Some(stk)) = b.passive_stacks.as_ref().and_then(|m| m.get(k)) else { continue };
            let steps = pg.thr.iter().flatten().filter(|t| *stk >= **t).count() as f64;
            pg.adj.iter().map(|(s, v)| (s.clone(), pg.per.unwrap_or(*v) * steps)).collect()
        } else {
            pg.adj.clone()
        };
        a.apply(&cur, label(&pg.n));
        for (s, v) in &cur {
            if s == "CriticalChance" {
                passive_crit += v
            }
        }
    }
    for sp in stack_passives(b) {
        let lv = stack_lv_for(b, &sp.k);
        if lv == 0 {
            continue;
        }
        let pg = sp.pg;
        let cur: Adj = pg.adj.iter().map(|(s, v)| (s.clone(), pg.per.unwrap_or(*v) * lv as f64)).collect();
        let lab = if sp.learned { pg.n.clone() } else { pg.an.clone().filter(|x| !x.is_empty()).or(pg.n.clone()) };
        a.apply(&cur, lab.as_deref());
        for (s, v) in &cur {
            if s == "CriticalChance" {
                passive_crit += v
            }
        }
    }
    if let Some(x) = b.extra_adj.as_ref() {
        a.apply(x, Some("Environment"));
    }
    let mut tal_bump = 0i64;
    for k in &b.insc {
        for r in d.insc.iter().filter(|r| &r.k == k) {
            a.apply(&r.adj, Some(&r.n));
        }
        if *k == d.consts.perfection.k {
            tal_bump = d.consts.perfection.bump;
        }
    }
    for slot in &d.slots {
        let Some(it) = b.eq.get(slot).filter(|k| !k.is_empty()).and_then(|k| d.items.get(k)) else { continue };
        if it.hp != 0.0 && !it.hp.is_nan() {
            add(&mut a.flat, "MaxHealthTotal", it.hp);
            Acc::rec(&mut a.src_flat, "MaxHealthTotal", Some(&it.n), it.hp);
        }
        if it.pa != 0.0 && !it.pa.is_nan() {
            add(&mut a.flat, "PhysicalAttackPower", it.pa);
            Acc::rec(&mut a.src_flat, "PhysicalAttackPower", Some(&it.n), it.pa);
        }
        if it.ma != 0.0 && !it.ma.is_nan() {
            add(&mut a.flat, "MagicAttackPower", it.ma);
            Acc::rec(&mut a.src_flat, "MagicAttackPower", Some(&it.n), it.ma);
        }
        if let Some(x) = it.adds.as_ref() {
            a.apply(x, Some(&it.n));
        }
        if let Some(x) = it.aadj.as_ref() {
            a.apply(x, Some(&it.n));
        }
        for p in b.tags.get(slot).into_iter().flatten() {
            let Some(p) = p else { continue };
            let Some(idx) = p.idx() else { continue };
            if idx < 0.0 || idx.fract() != 0.0 {
                continue;
            }
            let Some(tg) = it.tags.as_ref().and_then(|t| t.get(idx as usize)) else { continue };
            let val = match p.val() {
                Some(v) if p.is_obj() => v,
                _ => tg.1 + (tg.2 - tg.1) * q,
            };
            a.apply(&[(tg.0.clone(), val)], Some(&it.n));
        }
    }
    // generation % rolls: race + background, per major
    let mut gen_pct: IndexMap<String, f64> = IndexMap::new();
    let mut src_roll: IndexMap<String, Vec<Row>> = IndexMap::new();
    let srcs: [(Option<&Vec<(f64, f64)>>, Option<&str>); 2] = [
        (race.and_then(|r| r.rolls.as_ref()), race.map(|r| r.n.as_str())),
        (bg.and_then(|g| g.rolls.as_ref()), bg.map(|g| g.n.as_str())),
    ];
    for (rolls, n) in srcs {
        let Some(rolls) = rolls else { continue };
        for (i, m) in maj.iter().enumerate() {
            let r = rolls[i];
            if r.0 != 0.0 || r.1 != 0.0 {
                let v = r.0 + (r.1 - r.0) * q;
                add(&mut gen_pct, m, v);
                Acc::rec(&mut src_roll, m, n, v);
            }
        }
    }
    let mut majors: IndexMap<String, f64> = IndexMap::new();
    let mut bd: IndexMap<String, Vec<Row>> = IndexMap::new();
    let mut mraw: IndexMap<String, f64> = IndexMap::new();
    let mut mflat: IndexMap<String, f64> = IndexMap::new();
    for m in maj {
        let b0 = d.consts.major_base.get(m).copied().unwrap_or(f64::NAN);
        let mut rows = vec![Row { label: "Base".into(), v: b0 }];
        let fl = get(&a.flat, m);
        let mut sub;
        let mb = b.major_base.as_ref().and_then(|x| x.get(m)).copied().flatten();
        if let Some(mbv) = mb {
            if mbv.abs() > 0.049 {
                rows.push(Row { label: "Level-ups + growth".into(), v: mbv });
            }
            for s in a.src_flat.get(m).into_iter().flatten() {
                rows.push(s.clone());
            }
            sub = b0 + mbv + fl;
        } else {
            let t = b.tal.get(m).map(String::as_str).filter(|s| !s.is_empty()).unwrap_or("Moderate");
            let tier = (d.tier_idx(t) as i64 + tal_bump).min(4) as usize;
            let rg = d.tiers.range.get(&d.tiers.order[tier]).copied().unwrap_or((f64::NAN, f64::NAN));
            let gain = growth_per_level(rg, off, q) * (lvl - 1.0);
            let baked = gen_bake(b0, get(&gen_pct, m));
            let mut rsum = 0.0;
            for s in src_roll.get(m).into_iter().flatten() {
                let dd = b0 * s.v / 100.0;
                rsum += dd;
                if dd.abs() > 0.049 {
                    rows.push(Row { label: format!("{} roll", s.label), v: dd });
                }
            }
            let cut = baked - b0 - rsum;
            if cut.abs() > 0.049 {
                rows.push(Row { label: "Rounded down at recruit".into(), v: cut });
            }
            if gain.abs() > 0.049 {
                rows.push(Row { label: "Talent growth".into(), v: gain });
            }
            for s in a.src_flat.get(m).into_iter().flatten() {
                rows.push(s.clone());
            }
            sub = baked + gain + fl;
        }
        let exv = elix_amount(b, m);
        if exv != 0.0 {
            rows.push(Row { label: elix_label(b, m), v: exv });
            sub += exv;
        }
        for s in a.src_pct.get(m).into_iter().flatten() {
            let dd = sub * s.v / 100.0;
            if dd.abs() > 0.049 {
                rows.push(Row { label: s.label.clone(), v: dd });
            }
        }
        majors.insert(m.clone(), sub * (1.0 + get(&a.pct, m) / 100.0));
        mraw.insert(m.clone(), sub);
        mflat.insert(m.clone(), fl);
        bd.insert(m.clone(), rows);
    }
    for m in maj {
        let Some(mv) = b.manual_val(m) else { continue };
        majors.insert(m.clone(), mv);
        mraw.insert(m.clone(), mv / (1.0 + get(&a.pct, m) / 100.0));
    }
    let mut out: IndexMap<String, f64> = IndexMap::new();
    for e in &d.sv {
        if e.g == "major" {
            out.insert(e.k.clone(), majors.get(&e.k).copied().unwrap_or(f64::NAN));
            continue;
        }
        let mut rows = vec![Row { label: "Base".into(), v: e.d }];
        let mut v = e.d;
        for i in 0..6 {
            let c = e.c[i] * majors.get(&maj[i]).copied().unwrap_or(f64::NAN);
            if c.abs() > 0.049 {
                let l = d.statn.get(&maj[i]).filter(|s| !s.is_empty()).cloned().unwrap_or_else(|| maj[i].clone());
                rows.push(Row { label: l, v: c });
                v += c;
            }
        }
        for s in a.src_flat.get(&e.k).into_iter().flatten() {
            rows.push(s.clone());
        }
        v += get(&a.flat, &e.k);
        let sub = v;
        for s in a.src_pct.get(&e.k).into_iter().flatten() {
            let dd = sub * s.v / 100.0;
            if dd.abs() > 0.049 {
                rows.push(Row { label: s.label.clone(), v: dd });
            }
        }
        v *= 1.0 + get(&a.pct, &e.k) / 100.0;
        if d.consts.fin_mul {
            let f = get(&a.fin, &e.k);
            if f != 0.0 && !f.is_nan() {
                v *= 1.0 + f / 100.0;
            }
        }
        out.insert(e.k.clone(), v);
        bd.insert(e.k.clone(), rows);
    }
    let known = |k: &str| d.sv.iter().any(|e| e.k == k);
    let mut leftover = vec![];
    for (k, v) in &a.flat {
        if !known(k) {
            leftover.push((k.clone(), *v, 0u8));
        }
    }
    for (k, v) in &a.pct {
        if !known(k) {
            leftover.push((format!("{k}PerModifier"), *v, 1u8));
        }
    }
    let mut manual_keys = vec![];
    for k in b.manual.keys() {
        let Some(mv) = b.manual_val(k) else { continue };
        out.insert(k.clone(), mv);
        manual_keys.push(k.clone());
    }
    let hit = hit_bonus_of(b, &out, &bd);
    Sheet { majors, majors_raw: mraw, major_flat: mflat, st: out, bd, leftover, tal_bump, manual_keys, passive_crit, pct: a.pct, hit }
}

pub fn elix_amount(b: &Build, m: &str) -> f64 {
    let n = b.elix_uses(m);
    match dsb().elix_row(m) {
        Some(e) if n != 0 => n as f64 * e.per,
        _ => 0.0,
    }
}
fn elix_label(b: &Build, m: &str) -> String {
    match dsb().elix_row(m) {
        Some(e) => format!("{} x{}", e.n, b.elix_uses(m)),
        None => m.to_string(),
    }
}

/// estimates(): average hit, DPS and effective HP
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Estimates {
    pub phys_hit: f64,
    pub mag_hit: f64,
    pub hit: f64,
    pub dps: f64,
    pub ehp_p: f64,
    pub ehp_m: f64,
}
pub fn estimates(st: &IndexMap<String, f64>, magic_weapon: bool) -> Estimates {
    let crit = get(st, "CriticalChance").max(0.0).min(100.0) / 100.0;
    let cdb = get(st, "CriticalDamageBonus") / 100.0;
    let fdm = match st.get("FinalDamageMultiplier").copied() {
        Some(v) if v != 0.0 && !v.is_nan() => v,
        _ => 1.0,
    };
    let phys = get(st, "PhysicalAttackPower")
        * (1.0 + crit * cdb)
        * (1.0 + (get(st, "DamageDealtBonus") + get(st, "PhysicalDamageDealtBonus")) / 100.0)
        * fdm;
    let mag = get(st, "MagicAttackPower")
        * (1.0 + crit * cdb)
        * (1.0 + (get(st, "DamageDealtBonus") + get(st, "MagicDamageDealtBonus")) / 100.0)
        * fdm;
    let hit = if magic_weapon { mag } else { phys };
    Estimates {
        phys_hit: phys,
        mag_hit: mag,
        hit,
        dps: hit * get(st, "AttackSpeed"),
        ehp_p: get(st, "MaxHealthTotal") * (100.0 + get(st, "PhysicalResistance")) / 100.0,
        ehp_m: get(st, "MaxHealthTotal") * (100.0 + get(st, "MagicResistance")) / 100.0,
    }
}
/// % less damage taken for a resistance value
pub fn resist_pct(r: f64) -> f64 {
    100.0 * r / (100.0 + r)
}
