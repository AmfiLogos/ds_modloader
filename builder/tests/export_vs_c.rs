//! The Rust export (src/export.rs) against the PvP mod's own C (ext/pvp/src/pvp_build.c + pvp_wiki.c + fe/), on the
//! same links with the same seed: every entry must be byte-identical (closure, closure_hash, progress_sig,
//! wiki_hash, guids), every refusal the same text, the notes and the stored majors the same.
//! Cases: ext/pvp/tools/make_build_cases.js (random playable builds, --edge, --team 3) + refusals made here.
//! Needs `cc`, zlib, node and the ext sources; skipped with a note when one is missing.
//! The full comparison goes to tests/c_harness/last_run.txt.
use ds_unit_builder::codec;
use ds_unit_builder::export::{self, entries_from_link, wire_fp, wire_tables};
use ds_unit_builder::Build;
use serde_json::Value;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

const SEED: u64 = 20261014;
const UID: &str = "00112233445566778899aabbccddeeff";

fn home() -> PathBuf {
    PathBuf::from(std::env::var("HOME").unwrap_or_default())
}
fn harness() -> Option<PathBuf> {
    let src = home().join("ds_multiplayer/ext/pvp/src");
    let me = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/c_harness/harness.c");
    if !src.join("pvp_build.c").exists() || !me.exists() {
        println!("NOTE: ext/pvp sources or the harness are missing: test skipped");
        return None;
    }
    let out = Path::new(env!("CARGO_TARGET_TMPDIR")).join("pvp_export_harness");
    let st = Command::new("nice")
        .args(["-n", "19", "cc", "-O1", "-I"])
        .arg(&src)
        .arg(&me)
        .args(["pvp_build.c", "pvp_wiki.c", "fe/fe_json.c", "fe/fe_save.c"].map(|f| src.join(f)))
        .args(["-lz", "-lm", "-o"])
        .arg(&out)
        .status();
    match st {
        Ok(s) if s.success() => Some(out),
        _ => {
            println!("NOTE: cannot build the C harness (cc / zlib missing?): test skipped");
            None
        }
    }
}
fn node_cases(args: &[&str]) -> Vec<Value> {
    let tool = home().join("ds_multiplayer/ext/pvp/tools/make_build_cases.js");
    let o = Command::new("nice").args(["-n", "19", "node"]).arg(&tool).args(args).output().expect("node");
    String::from_utf8_lossy(&o.stdout).lines().filter_map(|l| serde_json::from_str(l).ok()).collect()
}

/// the Rust side in the harness's text format
fn rust_block(link: &str) -> String {
    let mut s = String::new();
    match entries_from_link(link, UID, Some(SEED)) {
        Err(e) => s += &format!("ERR {}\n", e.replace('\\', "\\\\").replace('\n', "\\n")),
        Ok(es) => {
            let maj = export::stored_majors_from_link(link).unwrap();
            for (i, e) in es.iter().enumerate() {
                let notes: String = e.notes.iter().map(|l| format!("{l}\n")).collect();
                s += &format!("E {}\nN {}\nM", e.json, notes.replace('\\', "\\\\").replace('\n', "\\n"));
                for v in maj[i] {
                    s += &format!(" {:016x}", v.to_bits());
                }
                s += "\n";
            }
        }
    }
    s
}

fn tables_json() -> Value {
    serde_json::from_str(&std::fs::read_to_string(Path::new(env!("CARGO_MANIFEST_DIR")).join("data/unit_tables.json")).unwrap()).unwrap()
}

/// refusal and odd-link cases made from the generated builds
fn refusals(builds: &[Build]) -> Vec<(String, String)> {
    let t = tables_json();
    let mut out = vec![];
    let items = t["items"].as_object().unwrap();
    let slot_names: Vec<String> = codec::idx().slot.clone();
    let item_of_slot = |s: usize| items.iter().find(|(_, v)| v["slot"].as_i64() == Some(s as i64)).map(|(k, _)| k.clone()).unwrap();
    let trees_of = |b: &Build| -> Vec<String> {
        let mut v: Vec<String> = vec![];
        if let Some(bg) = &b.bg {
            for k in ["bg_mains", "bg_subs"] {
                for x in t[k][bg].as_array().into_iter().flatten() {
                    v.push(x.as_str().unwrap().into())
                }
            }
        }
        v.extend(b.insctrees.iter().cloned());
        v
    };
    for (n, b0) in builds.iter().take(12).enumerate() {
        // an item in the wrong slot
        let mut b = b0.clone();
        b.eq.insert("Primary".into(), item_of_slot(2));
        out.push((format!("wrong slot {n}"), codec::encode_team(&[b])));
        let mut b = b0.clone();
        b.eq.insert(slot_names[4].clone(), item_of_slot(3));
        out.push((format!("body item in a ring slot {n}"), codec::encode_team(&[b])));
        // a skill outside the unit's trees
        let own = trees_of(b0);
        if let Some((sk, _)) = t["skills"].as_object().unwrap().iter().find(|(_, v)| v["tree"].as_str().map(|x| !own.contains(&x.to_string())).unwrap_or(false)) {
            let mut b = b0.clone();
            b.learned.push(sk.clone());
            out.push((format!("skill outside the trees {n}"), codec::encode_team(&[b])));
        }
        // over budget: level 1 with every skill of its trees
        let mut b = b0.clone();
        b.lvl = Some(1.0);
        b.learned = t["skills"]
            .as_object()
            .unwrap()
            .iter()
            .filter(|(_, v)| v["tree"].as_str().map(|x| own.contains(&x.to_string())).unwrap_or(false))
            .map(|(k, _)| k.clone())
            .collect();
        out.push((format!("over budget {n}"), codec::encode_team(&[b])));
        // level over the cap, and the link's own level limit
        let mut b = b0.clone();
        b.lvl = Some(13.0 + n as f64);
        out.push((format!("level {}", 13 + n), codec::encode_team(&[b])));
        // manual non-major stat + a major + hunger + passive stack note + saved traits
        let mut b = b0.clone();
        b.manual.insert("CriticalChance".into(), serde_json::json!(12.5));
        b.manual.insert("Strength".into(), serde_json::json!(33.33));
        b.cond = vec!["AFFECTER_VeryHungry".into(), "AFFECTER_SevereExhausted".into()];
        let mut ps = indexmap::IndexMap::new();
        ps.insert("SKILL_BasicPunchAttack".to_string(), Some(3.0));
        b.passive_stacks = Some(ps);
        b.saved_inds = Some(b0.inds.clone()).filter(|v| !v.is_empty());
        out.push((format!("notes {n}"), codec::encode_team(&[b])));
    }
    // a race with no look
    for r in &codec::idx().race {
        if t["profiles"][r].as_array().map(|a| a.is_empty()).unwrap_or(true) {
            let mut b = builds[0].clone();
            b.race = Some(r.clone());
            b.bg = None;
            b.learned.clear();
            out.push((format!("race with no look {r}"), codec::encode_team(&[b])));
        }
    }
    // link-level refusals
    let b1 = codec::encode_team_v1(&builds[..1]);
    let mut raw = codec::b64url_dec(&b1).unwrap();
    raw[2] ^= 0x55;
    out.push(("fingerprint mismatch".into(), codec::b64url_enc(&raw)));
    let raw = codec::b64url_dec(&b1).unwrap();
    out.push(("cut short".into(), codec::b64url_enc(&raw[..raw.len() - 2])));
    out.push(("nine units".into(), codec::encode_team(&builds[..9])));
    out.push(("eight units".into(), format!("https://dungeonsettlers.wiki/builder?team={}&x=1", codec::encode_team(&builds[..8]))));
    out.push(("not base64".into(), "team=ab$cd".into()));
    out.push(("empty code".into(), "https://dungeonsettlers.wiki/builder?team=".into()));
    out.push(("garbage".into(), "QUJDREVGR0g".into()));
    let mut raw = codec::b64url_dec(&b1).unwrap();
    raw[4] = 0x7f; // race index far outside the table
    out.push(("race index outside".into(), codec::b64url_enc(&raw)));
    out
}

#[test]
fn export_matches_c() {
    // the export's own link tables are the codec's
    let x = codec::idx();
    let mine = wire_tables();
    let theirs = [&x.race, &x.bg, &x.ind, &x.insc, &x.tree, &x.skill, &x.item, &x.stat, &x.slot, &x.soul, &x.acq];
    for ((name, a), b) in mine.iter().zip(theirs) {
        assert_eq!(*a, b, "link table {name} differs from codec::idx()");
    }
    assert_eq!(wire_fp(), x.fp, "fingerprint");

    let Some(h) = harness() else { return };
    if Command::new("node").arg("--version").output().is_err() {
        println!("NOTE: node missing: test skipped");
        return;
    }
    let seed_s = SEED.to_string();
    let mut cases: Vec<(String, String)> = vec![];
    let mut builds: Vec<Build> = vec![];
    for (tag, args) in [
        ("random", vec!["300", "11"]),
        ("random2", vec!["200", seed_s.as_str()]),
        ("edge", vec!["8", "5", "--edge"]),
        ("team3", vec!["60", "23", "--team", "3"]),
    ] {
        for (i, c) in node_cases(&args).iter().enumerate() {
            cases.push((format!("{tag} {i}"), c["link"].as_str().unwrap().to_string()));
            if let Some(b) = c.get("build") {
                if let Ok(b) = serde_json::from_value::<Build>(b.clone()) {
                    builds.push(b)
                }
            }
        }
    }
    assert!(cases.len() >= 300, "make_build_cases.js gave {} cases", cases.len());
    // the same builds through the launcher's own encoder (entries_from_builds path)
    for (i, b) in builds.iter().take(60).enumerate() {
        cases.push((format!("launcher-encoded {i}"), codec::encode_team(std::slice::from_ref(b))));
    }
    cases.extend(refusals(&builds));

    let mut child = Command::new(&h).arg(SEED.to_string()).arg(UID).stdin(Stdio::piped()).stdout(Stdio::piped()).spawn().unwrap();
    {
        let mut si = child.stdin.take().unwrap();
        for (_, l) in &cases {
            writeln!(si, "{l}").unwrap();
        }
    }
    let out = child.wait_with_output().unwrap();
    let text = String::from_utf8_lossy(&out.stdout).into_owned();
    let mut blocks: Vec<String> = vec![];
    for l in text.lines() {
        if l.starts_with("L ") {
            blocks.push(String::new());
        } else if let Some(b) = blocks.last_mut() {
            *b += l;
            *b += "\n";
        }
    }
    assert_eq!(blocks.len(), cases.len(), "harness output count");
    let (mut same, mut refused, mut units) = (0, 0, 0);
    let mut diffs = vec![];
    let mut log = String::new();
    for ((name, link), c) in cases.iter().zip(&blocks) {
        let r = rust_block(link);
        if c.starts_with("ERR") {
            refused += 1
        }
        units += c.lines().filter(|l| l.starts_with("E ")).count();
        if &r == c {
            same += 1;
            log += &format!("OK   {name}: {}\n", c.lines().next().unwrap_or("").chars().take(140).collect::<String>());
        } else {
            let (cl, rl): (Vec<&str>, Vec<&str>) = (c.lines().collect(), r.lines().collect());
            let k = (0..cl.len().max(rl.len())).find(|i| cl.get(*i) != rl.get(*i)).unwrap_or(0);
            let (a, b) = (cl.get(k).copied().unwrap_or(""), rl.get(k).copied().unwrap_or(""));
            let p = a.bytes().zip(b.bytes()).position(|(x, y)| x != y).unwrap_or(a.len().min(b.len()));
            let ctx = |s: &str| s.get(p.saturating_sub(80)..(p + 80).min(s.len())).unwrap_or("").to_string();
            diffs.push(format!("{name}: line {k} at byte {p}\n  C:    ...{}...\n  Rust: ...{}...\n  link: {link}", ctx(a), ctx(b)));
        }
    }
    let summary = format!(
        "cases {} (units built {}, refused {}), identical {}, differences {}",
        cases.len(),
        units,
        refused,
        same,
        diffs.len()
    );
    let path = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/c_harness/last_run.txt");
    let _ = std::fs::write(&path, format!("{summary}\n\n{}\n{log}", diffs.join("\n")));
    println!("{summary}");
    for d in diffs.iter().take(10) {
        println!("{d}");
    }
    assert!(diffs.is_empty(), "{summary} (details: {})", path.display());
}
