//! The engine against the wiki builder itself: builder.js (site/builder) runs in node (tests/node/builder_ref.js)
//! on the same random builds, and every number must be the SAME double (no tolerance). Also the share links both
//! ways (Rust codes byte-equal to the builder's, each side decodes the other's) and validate().
//! Needs node and ~/dungeonsettlersdb/site/builder (DSB_DIR overrides); skipped with a note when missing.
//! Builds: 400 random (tests/node/builder_ref.js gen: legal and illegal picks, imports, typed stats, elixirs,
//! needs, stacking passives, rolls...). Set DSB_CASES=N for more.
use ds_unit_builder::codec;
use ds_unit_builder::compute::{self, Sheet};
use ds_unit_builder::model::Build;
use ds_unit_builder::validate::validate;
use serde_json::{json, Value};
use std::path::PathBuf;
use std::process::{Command, Stdio};

fn node_ok() -> bool {
    let dir = std::env::var("DSB_DIR").map(PathBuf::from).unwrap_or_else(|_| {
        PathBuf::from(std::env::var("HOME").unwrap_or_default()).join("dungeonsettlersdb/site/builder")
    });
    if !dir.join("builder.js").exists() {
        eprintln!("SKIP: no wiki builder at {}", dir.display());
        return false;
    }
    if Command::new("node").arg("--version").output().is_err() {
        eprintln!("SKIP: node not found");
        return false;
    }
    true
}
fn node(args: &[&str], input: &str) -> Vec<Value> {
    // input through a file, not a pipe: test threads spawn in parallel and a pipe end can leak into a sibling child
    use std::sync::atomic::{AtomicUsize, Ordering};
    static N: AtomicUsize = AtomicUsize::new(0);
    let script = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/node/builder_ref.js");
    let inp = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join(format!("dsb_in_{}_{}.jsonl", std::process::id(), N.fetch_add(1, Ordering::SeqCst)));
    std::fs::write(&inp, input).unwrap();
    let out = Command::new("node").arg(&script).args(args).env("DSB_IN", &inp).stdin(Stdio::null()).stderr(Stdio::inherit()).output().expect("node");
    let _ = std::fs::remove_file(&inp);
    assert!(out.status.success(), "node {:?} failed", args);
    String::from_utf8(out.stdout).unwrap().lines().filter(|l| !l.trim().is_empty()).map(|l| serde_json::from_str(l).unwrap()).collect()
}
fn cases() -> Vec<Value> {
    let n = std::env::var("DSB_CASES").unwrap_or_else(|_| "400".into());
    let mut v = node(&["gen", &n, "11"], "");
    v.extend(node(&["gen", "100", "424242"], ""));
    v
}
fn jsonl(v: &[Value]) -> String {
    v.iter().map(|x| x.to_string() + "\n").collect()
}

/// deep compare: numbers must be the same double (null = NaN / missing), objects compare by key (missing = null)
fn diff(path: &str, a: &Value, b: &Value, out: &mut Vec<String>) {
    match (a, b) {
        (Value::Number(x), Value::Number(y)) => {
            let (x, y) = (x.as_f64().unwrap(), y.as_f64().unwrap());
            if x != y {
                out.push(format!("{path}: rust {x:?} / builder {y:?}"));
            }
        }
        (Value::Object(x), Value::Object(y)) => {
            let mut keys: Vec<&String> = x.keys().collect();
            for k in y.keys() {
                if !x.contains_key(k) {
                    keys.push(k)
                }
            }
            for k in keys {
                diff(&format!("{path}.{k}"), x.get(k).unwrap_or(&Value::Null), y.get(k).unwrap_or(&Value::Null), out);
            }
        }
        (Value::Array(x), Value::Array(y)) => {
            if x.len() != y.len() {
                out.push(format!("{path}: length rust {} / builder {}", x.len(), y.len()));
                return;
            }
            for (i, (p, q)) in x.iter().zip(y).enumerate() {
                diff(&format!("{path}[{i}]"), p, q, out);
            }
        }
        _ => {
            if a != b {
                out.push(format!("{path}: rust {a} / builder {b}"));
            }
        }
    }
}

fn rust_sheet(b: &Build) -> Value {
    let s: Sheet = compute::compute(b);
    let st = s.st.clone();
    let learned_cost: Vec<Value> = b
        .learned
        .iter()
        .map(|k| {
            let t = compute::tree_of_skill_for(k, b);
            let c = match &t {
                Some(t) if ds_unit_builder::data::dsb().skills.contains_key(k) => json!(compute::skill_cost(k, t, &st)),
                _ => Value::Null,
            };
            json!([k, t, c])
        })
        .collect();
    let stack: Vec<Value> = compute::stack_passives(b).iter().map(|sp| json!([sp.k, compute::stack_lv_for(b, &sp.k), sp.learned])).collect();
    let mut v = serde_json::to_value(&s).unwrap();
    v["aux"] = json!({
        "activeTrees": compute::active_trees(b), "mainSP": compute::main_sp_budget(b), "subSP": compute::sub_sp_budget(b),
        "inscSlots": compute::insc_slots(b), "inscCap": compute::insc_cap(b), "learnedCost": learned_cost,
        "skillSets": compute::unit_skill_sets(b, None), "quick": compute::quick_for(b), "stack": stack,
        "est": compute::estimates(&st, false), "estM": compute::estimates(&st, true),
    });
    v
}

#[test]
fn sheet_matches_builder() {
    if !node_ok() {
        return;
    }
    let cs = cases();
    let exp = node(&["sheet"], &jsonl(&cs));
    assert_eq!(exp.len(), cs.len());
    let mut bad = 0;
    let mut nums = 0usize;
    let mut report = vec![];
    for (i, (c, e)) in cs.iter().zip(&exp).enumerate() {
        let b: Build = serde_json::from_value(c.clone()).expect("build json");
        let got = rust_sheet(&b);
        // the stat order is part of the sheet
        let ko: Vec<&String> = got["st"].as_object().unwrap().keys().collect();
        let ke: Vec<&String> = e["st"].as_object().unwrap().keys().collect();
        let mut d = vec![];
        if ko.len() == ke.len() && ko.iter().zip(&ke).any(|(a, b)| a != b) {
            d.push("st: key order differs".to_string());
        }
        diff("", &got, e, &mut d);
        nums += count_nums(e);
        if !d.is_empty() {
            bad += 1;
            report.push(format!("build {i}: {} difference(s)\n  {}\n  build: {}", d.len(), d.iter().take(12).cloned().collect::<Vec<_>>().join("\n  "), c));
        }
    }
    eprintln!("sheet: {} builds, {} numbers compared, {} build(s) with a difference", cs.len(), nums, bad);
    for r in report.iter().take(10) {
        eprintln!("{r}");
    }
    assert_eq!(bad, 0);
}

fn count_nums(v: &Value) -> usize {
    match v {
        Value::Number(_) => 1,
        Value::Array(a) => a.iter().map(count_nums).sum(),
        Value::Object(o) => o.values().map(count_nums).sum(),
        _ => 0,
    }
}

#[test]
fn links_match_builder() {
    if !node_ok() {
        return;
    }
    let cs = cases();
    let enc = node(&["encode"], &jsonl(&cs));
    let mut bad = vec![];
    let mut rust_v2 = vec![];
    for (i, (c, e)) in cs.iter().zip(&enc).enumerate() {
        let b: Build = serde_json::from_value(c.clone()).unwrap();
        // the same bytes as the builder's encoder
        let v1 = codec::encode_team_v1(std::slice::from_ref(&b));
        if v1 != e["v1"].as_str().unwrap() {
            bad.push(format!("build {i}: v1 code differs\n  rust    {v1}\n  builder {}", e["v1"]));
        }
        // our decode of the builder's codes (raw and deflated) = the builder's own decode
        let want: Build = serde_json::from_value(e["dec"]["units"][0].clone()).unwrap();
        for k in ["v1", "v2"] {
            let got = codec::decode_code(e[k].as_str().unwrap()).expect("decode");
            if got.units.len() != 1 || got.units[0] != want {
                bad.push(format!("build {i}: decode of builder {k} differs\n  rust    {}\n  builder {}", serde_json::to_string(&got.units).unwrap(), e["dec"]["units"][0]));
            }
            if got.warn != e["dec"]["warn"].as_bool().unwrap() || json!(got.gone) != e["dec"]["gone"] {
                bad.push(format!("build {i}: warn/gone differ"));
            }
        }
        // the full #b= JSON link
        let full = codec::decode_link(&format!("{}#b={}", codec::WIKI_BUILDER_URL, e["b64"].as_str().unwrap())).unwrap();
        if full.units[0] != b {
            bad.push(format!("build {i}: #b= JSON link differs"));
        }
        // one-unit compact link through the link parser
        let solo = codec::decode_link(&codec::solo_link(&b)).unwrap();
        if solo.units[0] != want {
            bad.push(format!("build {i}: #b=~ link differs"));
        }
        rust_v2.push(Value::String(codec::encode_team(std::slice::from_ref(&b))));
    }
    // the builder decodes our compressed codes to the same build
    let dec = node(&["decode"], &rust_v2.iter().map(|v| v.as_str().unwrap().to_string() + "\n").collect::<String>());
    for (i, (dv, e)) in dec.iter().zip(&enc).enumerate() {
        if dv["units"] != e["dec"]["units"] {
            bad.push(format!("build {i}: builder decode of the rust code differs"));
        }
    }
    // teams of 3
    let mut teams = 0;
    for (ti, grp) in cs.chunks(3).enumerate().take(60) {
        let t = node(&["team"], &jsonl(grp));
        let builds: Vec<Build> = grp.iter().map(|c| serde_json::from_value(c.clone()).unwrap()).collect();
        if codec::encode_team_v1(&builds) != t[0]["v1"].as_str().unwrap() {
            bad.push(format!("team {ti}: v1 code differs"));
        }
        let want: Vec<Build> = serde_json::from_value(t[0]["dec"]["units"].clone()).unwrap();
        for k in ["v1", "v2"] {
            let link = format!("{}?team={}", codec::WIKI_BUILDER_URL, t[0][k].as_str().unwrap());
            if codec::decode_link(&link).map(|d| d.units) != Ok(want.clone()) {
                bad.push(format!("team {ti}: decode of builder {k} differs"));
            }
        }
        teams += 1;
    }
    eprintln!("links: {} builds and {} teams, {} difference(s)", cs.len(), teams, bad.len());
    for r in bad.iter().take(10) {
        eprintln!("{r}");
    }
    assert!(bad.is_empty());
}

#[test]
fn validate_matches_builder() {
    if !node_ok() {
        return;
    }
    let cs = cases();
    let exp = node(&["validate"], &jsonl(&cs));
    let keys = [
        "race", "bg", "lvl", "inds", "portrait", "insctrees", "insc", "souls", "acqInds", "elix", "cond", "eq", "tags", "learned", "shelf",
        "shelfTags", "manual", "advanced",
    ];
    let mut bad = vec![];
    for (i, (c, e)) in cs.iter().zip(&exp).enumerate() {
        let mut b: Build = serde_json::from_value(c.clone()).unwrap();
        validate(&mut b);
        let got = serde_json::to_value(&b).unwrap();
        let mut d = vec![];
        for k in keys {
            diff(k, got.get(k).unwrap_or(&Value::Null), e.get(k).unwrap_or(&Value::Null), &mut d);
        }
        // a second pass changes nothing (the builder's validate is idempotent)
        let mut b2 = b.clone();
        validate(&mut b2);
        if b2 != b {
            d.push("second validate changed the build".into());
        }
        if !d.is_empty() {
            bad.push(format!("build {i}: {}\n  build: {}", d.iter().take(8).cloned().collect::<Vec<_>>().join("\n  "), c));
        }
    }
    eprintln!("validate: {} builds, {} with a difference", cs.len(), bad.len());
    for r in bad.iter().take(8) {
        eprintln!("{r}");
    }
    assert!(bad.is_empty());
}

#[test]
fn builder_selftests() {
    // the builder's own selftest builds (D.selftest), expected values to 0.01 as runSelftests checks them
    for t in &ds_unit_builder::data::dsb().selftest {
        let b: Build = serde_json::from_value(t["b"].clone()).unwrap();
        let s = compute::compute(&b);
        for (k, v) in t["expect"].as_object().unwrap() {
            let g = (s.st[k] * 10000.0).round() / 10000.0;
            assert!((g - v.as_f64().unwrap()).abs() < 0.01, "{}: {} expected {} got {}", t["name"], k, v, g);
        }
    }
}
