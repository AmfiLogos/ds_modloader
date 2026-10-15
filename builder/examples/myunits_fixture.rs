//! Cross-check fixture for the game mod's "My Units" reader (ds_mp src/myunits.h, ext/myunits): a library of 30
//! varied units + 3 teams, written exactly as the launcher writes it, and the team link of each unit.
//!   cargo run -p ds-unit-builder --example myunits_fixture -- [out dir]
//! Default out dir: ~/ds_mp_myunits/ext/myunits/fixtures. Writes launcher_library.json and launcher_links.json
//! (builder JSON only: keys and numbers, no game files). Deterministic builds (seeded); ids and times are fresh.
use ds_unit_builder::codec;
use ds_unit_builder::compute::active_trees;
use ds_unit_builder::data::dsb;
use ds_unit_builder::export::Rng;
use ds_unit_builder::model::{Build, TagPick};
use ds_unit_builder::store::{Store, UnitsFile};
use ds_unit_builder::validate::{set_slot_item, validate};
use indexmap::IndexMap;
use serde_json::json;
use std::path::PathBuf;

const N: usize = 30;

struct R(Rng);
impl R {
    fn n(&mut self, n: usize) -> usize {
        if n == 0 {
            0
        } else {
            (self.0.next() % n as u64) as usize
        }
    }
    fn chance(&mut self, p: f64) -> bool {
        (self.0.next() % 1000) < (p * 1000.0) as u64
    }
    fn pick<'a, T>(&mut self, v: &'a [T]) -> &'a T {
        &v[self.n(v.len())]
    }
}

fn build(i: usize, r: &mut R) -> Build {
    let d = dsb();
    let mut b = Build::default_build();
    let race = d.races[i % d.races.len()].k.clone();
    b.race = Some(race.clone());
    b.lvl = Some(1.0 + r.n(12) as f64);
    b.roll = Some(r.pick(&["min", "avg", "max"]).to_string());
    let bgs: Vec<String> = d.races[i % d.races.len()].bg.iter().map(|p| p.0.clone()).filter(|k| d.bg.contains_key(k)).collect();
    if !bgs.is_empty() {
        b.bg = Some(r.pick(&bgs).clone());
    }
    for m in &d.majors {
        b.tal.insert(m.clone(), r.pick(&d.tiers.order).clone());
    }
    // names: plain, accents, quotes / backslash (JSON escapes), long
    b.name = Some(match i % 6 {
        0 => format!("Unit {i}"),
        1 => format!("Élodie Ünïcødé {i}"),
        2 => format!("\"Quoted\" \\ back {i}"),
        3 => format!("{}{i}", "Long".repeat(10)),
        4 => format!("日本語 {i}"),
        _ => format!("Plain{i}"),
    });
    if let Some(pool) = d.portraits.get(&race).filter(|p| !p.is_empty()) {
        if i % 5 != 4 {
            b.portrait = Some(r.pick(pool).clone());
        }
    }
    // gear, with rolls: explicit v, bare index, {i} without v, quick-set default {i, d: 1}
    for slot in d.slots.clone() {
        if !r.chance(0.7) {
            continue;
        }
        let ok: Vec<&String> = d.items.iter().filter(|(_, it)| it.slot == slot).map(|(k, _)| k).collect();
        if ok.is_empty() {
            continue;
        }
        let k = r.pick(&ok).to_string();
        set_slot_item(&mut b, &slot, Some(&k));
        let Some(tags) = d.items[&k].tags.as_ref().filter(|t| !t.is_empty()) else { continue };
        let mut picks = vec![];
        for _ in 0..1 + r.n(3) {
            let ti = r.n(tags.len());
            let (_, lo, hi) = &tags[ti];
            picks.push(Some(match r.n(4) {
                0 => TagPick::Obj { i: Some(ti as f64), v: Some(lo + (hi - lo) * (r.n(101) as f64 / 100.0)), d: None },
                1 => TagPick::Idx(ti as f64),
                2 => TagPick::Obj { i: Some(ti as f64), v: None, d: None },
                _ => TagPick::Obj { i: Some(ti as f64), v: None, d: Some(json!(1)) },
            }));
        }
        b.tags.insert(slot.clone(), picks);
    }
    // skills from the unit's trees
    let trees = active_trees(&b);
    let mut pool = vec![];
    for t in &trees {
        for row in &d.trees[t].rows {
            pool.extend(row.iter().flatten().cloned());
        }
    }
    for _ in 0..r.n(6) {
        if pool.is_empty() {
            break;
        }
        let k = r.pick(&pool).clone();
        if !b.learned.contains(&k) {
            b.learned.push(k);
        }
    }
    // milestone passives with banked stacks
    let grants: Vec<String> = d.passive_grants.iter().filter(|(_, g)| g.thr.as_ref().is_some_and(|t| !t.is_empty())).map(|(k, _)| k.clone()).collect();
    if i % 4 == 1 && !grants.is_empty() {
        let k = r.pick(&grants).clone();
        if !b.learned.contains(&k) {
            b.learned.push(k.clone());
        }
        let mut ps = IndexMap::new();
        ps.insert(k, Some([5.0, 30.0, 120.0, 2600.0][r.n(4)]));
        b.passive_stacks = Some(ps);
    }
    // elixirs, conditions, souls, typed stats
    if i % 3 == 0 {
        let m = r.pick(&d.majors).clone();
        b.elix.insert(m, json!(1 + r.n(5)));
    }
    let conds: Vec<String> = d.cond.iter().flat_map(|g| g.opts.iter().map(|o| o.k.clone())).collect();
    if i % 4 == 2 && !conds.is_empty() {
        b.cond.push(r.pick(&conds).clone());
    }
    let souls: Vec<String> = d.souls.keys().cloned().collect();
    if i % 5 == 3 && !souls.is_empty() {
        b.souls.push(r.pick(&souls).clone());
    }
    if i % 6 == 5 {
        b.manual.insert(r.pick(&d.majors).clone(), json!(20.0 + r.n(400) as f64 / 10.0));
        b.manual.insert(d.sv[r.n(d.sv.len())].k.clone(), json!("33.5"));
    }
    validate(&mut b);
    // validate() writes a value into every roll; the file format also allows picks without one (the builder's
    // older saves, hand edits): put some back so the reader sees {i} without v, a bare index and {i, d: 1}
    if let Some(picks) = b.tags.values_mut().find(|p| p.iter().flatten().count() > 0) {
        if let Some(Some(p)) = picks.iter_mut().find(|p| p.is_some()) {
            let ti = p.idx().unwrap_or(0.0);
            match i % 7 {
                1 => *p = TagPick::Obj { i: Some(ti), v: None, d: None },
                3 => *p = TagPick::Idx(ti),
                5 => *p = TagPick::Obj { i: Some(ti), v: None, d: Some(json!(1)) },
                _ => {}
            }
        }
    }
    b
}

fn main() {
    let out = std::env::args().nth(1).map(PathBuf::from).unwrap_or_else(|| {
        PathBuf::from(std::env::var("HOME").unwrap_or_default()).join("ds_mp_myunits/ext/myunits/fixtures")
    });
    std::fs::create_dir_all(&out).expect("out dir");
    let mut r = R(Rng::new(Some(0x6D79_756E_6974_73)));
    let builds: Vec<Build> = (0..N).map(|i| build(i, &mut r)).collect();

    // the store, through its own save (a scratch file, then the bytes copied: no .bak next to the fixture)
    let scratch = std::env::temp_dir().join(format!("dsub_fixture_{}", ds_unit_builder::store::new_id())).join("library.json");
    let mut s = Store { path: scratch.clone(), file: UnitsFile::default() };
    let units = s.import(builds).expect("import");
    let ids: Vec<String> = units.iter().map(|u| u.id.clone()).collect();
    s.team_create("Full Eight", ids[0..8].to_vec()).unwrap();
    s.team_create("Trio", vec![ids[12].clone(), ids[5].clone(), ids[29].clone()]).unwrap();
    s.team_create("Solo", vec![ids[17].clone()]).unwrap();
    s.save().expect("save");
    std::fs::copy(&scratch, out.join("launcher_library.json")).expect("copy library");
    let _ = std::fs::remove_dir_all(scratch.parent().unwrap());

    let links: Vec<_> = units.iter().map(|u| json!({ "id": u.id, "link": codec::team_link(std::slice::from_ref(&u.build)) })).collect();
    std::fs::write(out.join("launcher_links.json"), serde_json::to_vec_pretty(&links).unwrap()).expect("links");

    // what the fixture covers
    let bs: Vec<&Build> = units.iter().map(|u| &u.build).collect();
    let count = |f: &dyn Fn(&Build) -> bool| bs.iter().filter(|b| f(b)).count();
    let tag = |f: &dyn Fn(&TagPick) -> bool| bs.iter().filter(|b| b.tags.values().flatten().flatten().any(|t| f(t))).count();
    eprintln!(
        "{} units, {} teams -> {}\n  portrait {}  tags v {}  tags no v {}  bare idx {}  elix {}  cond {}  passiveStacks {}  souls {}  manual {}",
        units.len(),
        s.file.teams.len(),
        out.display(),
        count(&|b| b.portrait.is_some()),
        tag(&|t| t.val().is_some()),
        tag(&|t| t.is_obj() && t.val().is_none()),
        tag(&|t| !t.is_obj()),
        count(&|b| !b.elix.is_empty()),
        count(&|b| !b.cond.is_empty()),
        count(&|b| b.passive_stacks.as_ref().is_some_and(|p| !p.is_empty())),
        count(&|b| !b.souls.is_empty()),
        count(&|b| !b.manual.is_empty()),
    );
}
