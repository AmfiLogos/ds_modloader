//! PvP / Foreign Expedition export: a builder team -> the `{closure, manifest}` entries the PvP mod writes from a wiki
//! link (`cmd_wiki` in ~/ds_multiplayer/ext/pvp/src/pvp_units.c). Port of `wk_decode` / `wk_json` / `wk_hash`
//! (pvp_wiki.c), `pb_entry` / `pb_stored_majors` (pvp_build.c), `fe_new_guid` / `fe_new_id` / `fe_progress_sig`
//! (fe/fe_save.c) and `jparse` / `jdump` / `jcanon` / `jhash` (fe/fe_json.c). With the same seed (`fe_guid_seed`)
//! the entry is byte-identical to the C one (tests/export_vs_c.rs runs both).
//! Tables: data/unit_tables.json (gen/gen_unit_tables.js: the game tables of build 25846389 + the builder's link
//! index order), the same facts as the C headers pvp_unit_tables.h / pvp_wiki_tables.h.
//! The link is decoded HERE with the C decoder's rules (an index outside the tables refuses the link, as the mod
//! does), not with `codec::decode_link` (which drops such indices): links the C decoder cannot read (`#b=` forms)
//! go through `codec::decode_link` + `codec::encode_team` first, which is what the mod would get from the wiki.
//! Known divergences (all on links no encoder writes): an index >= 2^31 (negative after the C `(int)` cast) is
//! undefined behaviour in C; here a list / race / item index is refused as outside the table, a tag / stat /
//! passive-stack / portrait index is skipped. Duplicate object keys sort stably here (C qsort order unspecified).
use crate::codec;
use crate::model::Build;
use serde_json::Value;
use std::collections::HashMap;
use std::sync::OnceLock;

pub const WK_MAX_UNITS: usize = 8;
const WK_MAX_LIST: u32 = 64;
const WK_SLOTS: usize = 16;
const ZERO: &str = "00000000-0000-0000-0000-000000000000";

// ---------------------------------------------------------------- tables
struct Prof {
    key: String,
    portrait: String,
    vis: i32,
    gender: i32,
    head: i32,
    body: i32,
    skin: i32,
    facial: i32,
    eyes: i32,
    hair: i32,
}
struct ItemT {
    slot: i32,
    skills: Vec<(String, i32)>,
    tag_stat: Vec<String>,
    tag_lohi: Vec<[f32; 2]>,
    tagamount: i32,
    dur: i32,
    adj: Option<String>,
    onequip: String,
}
struct Pg {
    cond: bool,
    thr: Vec<i32>,
    aff: Option<String>,
    per: f32,
    adj: String,
}
struct Tables {
    game_version: String,
    fp: u32,
    race: Vec<String>,
    bg: Vec<String>,
    ind: Vec<String>,
    insc: Vec<String>,
    tree: Vec<String>,
    skill: Vec<String>,
    item: Vec<String>,
    stat: Vec<String>,
    slot: Vec<String>,
    soul: Vec<String>,
    acq: Vec<String>,
    maj: Vec<String>,
    tier: Vec<String>,
    item_tags: Vec<Vec<(f64, f64)>>,
    portraits: Vec<Option<Vec<String>>>,
    cond: Vec<Vec<String>>,
    talent: Vec<[[f32; 2]; 6]>,
    offer: Vec<f32>,
    sp: [i32; 4],
    max_level: f64,
    major_base: [f32; 6],
    race_roll: Vec<[[f32; 2]; 6]>,
    bg_roll: Vec<[[f32; 2]; 6]>,
    bg_mains: Vec<Vec<i32>>,
    bg_subs: Vec<Vec<i32>>,
    tree_kind: Vec<i32>,
    tree_apt: Vec<Option<String>>,
    tree_insc: Vec<Option<String>>,
    sk_passive: Vec<bool>,
    sk_tree: Vec<i32>,
    sk_sp: Vec<i32>,
    items: Vec<ItemT>,
    aff_adj: HashMap<String, String>,
    prof: Vec<Vec<Prof>>,
    race_name: Vec<String>,
    elixir: [f32; 6],
    pg: Vec<Option<Pg>>,
}

static UNIT_JSON: &str = include_str!("../data/unit_tables.json");
static TABLES: OnceLock<Tables> = OnceLock::new();

/// a float of the tables: the C header's literal text, parsed as a double (then `as f32` where C stores a float)
fn fnum(v: &Value) -> f64 {
    match v {
        Value::String(s) => s.parse::<f64>().unwrap_or(0.0),
        Value::Number(n) => n.as_f64().unwrap_or(0.0),
        _ => 0.0,
    }
}
fn inum(v: &Value) -> i32 {
    v.as_i64().map(|x| x as i32).unwrap_or_else(|| fnum(v) as i32)
}
fn strs(v: &Value) -> Vec<String> {
    v.as_array().map(|a| a.iter().map(|x| x.as_str().unwrap_or("").to_string()).collect()).unwrap_or_default()
}
fn ostr(v: &Value) -> Option<String> {
    v.as_str().map(str::to_string)
}
fn pairs6(v: &Value) -> [[f32; 2]; 6] {
    let mut o = [[0f32; 2]; 6];
    for (m, p) in v.as_array().into_iter().flatten().enumerate().take(6) {
        o[m] = [fnum(&p[0]) as f32, fnum(&p[1]) as f32];
    }
    o
}
fn arr6(v: &Value) -> [f32; 6] {
    let mut o = [0f32; 6];
    for (m, p) in v.as_array().into_iter().flatten().enumerate().take(6) {
        o[m] = fnum(p) as f32;
    }
    o
}

fn tables() -> &'static Tables {
    TABLES.get_or_init(|| {
        let j: Value = serde_json::from_str(UNIT_JSON).expect("data/unit_tables.json");
        let w = &j["wire"];
        let tree = strs(&w["tree"]);
        let ti = |t: &Value| t.as_str().and_then(|s| tree.iter().position(|x| x == s)).map(|i| i as i32).unwrap_or(-1);
        let race = strs(&w["race"]);
        let bg = strs(&w["bg"]);
        let skill = strs(&w["skill"]);
        let item = strs(&w["item"]);
        let skills: Vec<&Value> = skill.iter().map(|k| &j["skills"][k]).collect();
        Tables {
            game_version: j["game_version"].as_str().unwrap_or("").into(),
            fp: w["fp"].as_u64().unwrap_or(0) as u32,
            ind: strs(&w["ind"]),
            insc: strs(&w["insc"]),
            stat: strs(&w["stat"]),
            slot: strs(&w["slot"]),
            soul: strs(&w["soul"]),
            acq: strs(&w["acq"]),
            maj: strs(&w["maj"]),
            tier: strs(&w["tier"]),
            item_tags: w["item_tags"]
                .as_array()
                .into_iter()
                .flatten()
                .map(|t| t.as_array().into_iter().flatten().map(|p| (fnum(&p[0]), fnum(&p[1]))).collect())
                .collect(),
            portraits: w["portraits"].as_array().into_iter().flatten().map(|p| p.as_array().map(|_| strs(p))).collect(),
            cond: w["cond"].as_array().into_iter().flatten().map(strs).collect(),
            talent: j["talent"].as_array().into_iter().flatten().map(pairs6).collect(),
            offer: j["offer"].as_array().into_iter().flatten().map(|v| fnum(v) as f32).collect(),
            sp: {
                let mut s = [0; 4];
                for (i, v) in j["sp"].as_array().into_iter().flatten().enumerate().take(4) {
                    s[i] = inum(v)
                }
                s
            },
            max_level: fnum(&j["max_level"]),
            major_base: arr6(&j["major_base"]),
            race_roll: race.iter().map(|k| pairs6(&j["race_roll"][k])).collect(),
            bg_roll: bg.iter().map(|k| pairs6(&j["bg_roll"][k])).collect(),
            bg_mains: bg.iter().map(|k| j["bg_mains"][k].as_array().into_iter().flatten().map(ti).collect()).collect(),
            bg_subs: bg.iter().map(|k| j["bg_subs"][k].as_array().into_iter().flatten().map(ti).collect()).collect(),
            tree_kind: tree.iter().map(|t| inum(&j["trees"][t]["kind"])).collect(),
            tree_apt: tree.iter().map(|t| ostr(&j["trees"][t]["apt"])).collect(),
            tree_insc: tree.iter().map(|t| ostr(&j["trees"][t]["insc"])).collect(),
            sk_passive: skills.iter().map(|s| inum(&s["passive"]) != 0).collect(),
            sk_tree: skills.iter().map(|s| ti(&s["tree"])).collect(),
            sk_sp: skills.iter().map(|s| inum(&s["sp"])).collect(),
            items: item
                .iter()
                .map(|k| {
                    let r = &j["items"][k];
                    let tags = r["tags"].as_array().cloned().unwrap_or_default();
                    ItemT {
                        slot: r["slot"].as_i64().map(|x| x as i32).unwrap_or(-1),
                        skills: r["skills"]
                            .as_array()
                            .into_iter()
                            .flatten()
                            .map(|p| (p[0].as_str().unwrap_or("").to_string(), inum(&p[1])))
                            .collect(),
                        tag_stat: tags.iter().map(|t| t[0].as_str().unwrap_or("").to_string()).collect(),
                        tag_lohi: tags.iter().map(|t| [fnum(&t[1]) as f32, fnum(&t[2]) as f32]).collect(),
                        tagamount: inum(&r["tagamount"]),
                        dur: inum(&r["dur"]),
                        adj: ostr(&r["adj"]),
                        onequip: r["onequip"].as_str().unwrap_or("").into(),
                    }
                })
                .collect(),
            aff_adj: j["aff_adj"]
                .as_object()
                .map(|o| o.iter().map(|(k, v)| (k.clone(), v.as_str().unwrap_or("").to_string())).collect())
                .unwrap_or_default(),
            prof: race
                .iter()
                .map(|r| {
                    j["profiles"][r]
                        .as_array()
                        .into_iter()
                        .flatten()
                        .map(|p| Prof {
                            key: p["key"].as_str().unwrap_or("").into(),
                            portrait: p["portrait"].as_str().unwrap_or("").into(),
                            vis: inum(&p["vis"]),
                            gender: inum(&p["gender"]),
                            head: inum(&p["head"]),
                            body: inum(&p["body"]),
                            skin: inum(&p["skin"]),
                            facial: inum(&p["facial"]),
                            eyes: inum(&p["eyes"]),
                            hair: inum(&p["hair"]),
                        })
                        .collect()
                })
                .collect(),
            race_name: race.iter().map(|r| j["race_name"][r].as_str().unwrap_or("").to_string()).collect(),
            elixir: arr6(&j["elixir"]),
            pg: skill
                .iter()
                .map(|k| {
                    let g = &j["pg"][k];
                    g.is_object().then(|| Pg {
                        cond: inum(&g["cond"]) != 0,
                        thr: g["thr"].as_array().into_iter().flatten().map(inum).take(8).collect(),
                        aff: ostr(&g["aff"]),
                        per: fnum(&g["per"]) as f32,
                        adj: g["adj"].as_str().unwrap_or("").into(),
                    })
                })
                .collect(),
            race,
            bg,
            tree,
            skill,
            item,
        }
    })
}

/// the link index tables of the embedded export data (tests check them against `codec::idx()`)
pub fn wire_tables() -> Vec<(&'static str, &'static Vec<String>)> {
    let t = tables();
    vec![
        ("race", &t.race),
        ("bg", &t.bg),
        ("ind", &t.ind),
        ("insc", &t.insc),
        ("tree", &t.tree),
        ("skill", &t.skill),
        ("item", &t.item),
        ("stat", &t.stat),
        ("slot", &t.slot),
        ("soul", &t.soul),
        ("acq", &t.acq),
    ]
}
pub fn wire_fp() -> u32 {
    tables().fp
}

// ---------------------------------------------------------------- printf
/// C `%.6g`
pub fn fmt_g6(x: f64) -> String {
    if x.is_nan() {
        return if x.is_sign_negative() { "-nan".into() } else { "nan".into() };
    }
    if x.is_infinite() {
        return if x < 0.0 { "-inf".into() } else { "inf".into() };
    }
    let e = format!("{:.5e}", x);
    let (mant, exp) = e.split_once('e').unwrap();
    let xp: i32 = exp.parse().unwrap();
    let strip = |s: &str| -> String {
        if s.contains('.') {
            s.trim_end_matches('0').trim_end_matches('.').to_string()
        } else {
            s.to_string()
        }
    };
    if xp < -4 || xp >= 6 {
        format!("{}e{}{:02}", strip(mant), if xp < 0 { '-' } else { '+' }, xp.abs())
    } else {
        strip(&format!("{:.*}", (5 - xp) as usize, x))
    }
}
/// C `%.Nf`
fn fmt_f(x: f64, p: usize) -> String {
    if x.is_nan() {
        return if x.is_sign_negative() { "-nan".into() } else { "nan".into() };
    }
    if x.is_infinite() {
        return if x < 0.0 { "-inf".into() } else { "inf".into() };
    }
    format!("{:.*}", p, x)
}
/// C atof (strtod on the longest numeric prefix)
fn atof(s: &str) -> f64 {
    let b = s.as_bytes();
    let mut i = 0;
    while i < b.len() && (b[i] == b' ' || (9..=13).contains(&b[i])) {
        i += 1
    }
    let st = i;
    if i < b.len() && (b[i] == b'+' || b[i] == b'-') {
        i += 1
    }
    let d0 = i;
    while i < b.len() && b[i].is_ascii_digit() {
        i += 1
    }
    let mut nd = i - d0;
    if i < b.len() && b[i] == b'.' {
        i += 1;
        let f0 = i;
        while i < b.len() && b[i].is_ascii_digit() {
            i += 1
        }
        nd += i - f0;
    }
    if nd == 0 {
        return 0.0;
    }
    let mant_end = i;
    if i < b.len() && (b[i] == b'e' || b[i] == b'E') {
        let mut k = i + 1;
        if k < b.len() && (b[k] == b'+' || b[k] == b'-') {
            k += 1
        }
        let e0 = k;
        while k < b.len() && b[k].is_ascii_digit() {
            k += 1
        }
        if k > e0 {
            i = k
        } else {
            i = mant_end
        }
    }
    let t = &s[st..i];
    let t = t.strip_prefix('+').unwrap_or(t);
    let t = if t.starts_with('.') || t.starts_with("-.") { t.replacen('.', "0.", 1) } else { t.to_string() };
    t.trim_end_matches('.').parse::<f64>().or_else(|_| format!("{t}0").parse::<f64>()).unwrap_or(0.0)
}
fn rhe(x: f64) -> f64 {
    x.round_ties_even()
}

// ---------------------------------------------------------------- ids (fe_save.c)
/// fe_guid_seed + rnd64: splitmix64 when seeded, else random
pub struct Rng {
    state: u64,
}
impl Rng {
    pub fn new(seed: Option<u64>) -> Rng {
        match seed {
            Some(s) if s != 0 => Rng { state: s },
            _ => Rng { state: random_seed() },
        }
    }
    pub fn next(&mut self) -> u64 {
        self.state = self.state.wrapping_add(0x9E3779B97F4A7C15);
        let mut z = self.state;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58476D1CE4E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D049BB133111EB);
        z ^ (z >> 31)
    }
    pub fn guid(&mut self) -> String {
        let (a, c) = (self.next(), self.next());
        let mut b = [0u8; 16];
        b[..8].copy_from_slice(&a.to_le_bytes());
        b[8..].copy_from_slice(&c.to_le_bytes());
        b[6] = (b[6] & 0x0F) | 0x40;
        b[8] = (b[8] & 0x3F) | 0x80;
        let h: Vec<String> = b.iter().map(|x| format!("{:02x}", x)).collect();
        format!("{}-{}-{}-{}-{}", h[0..4].concat(), h[4..6].concat(), h[6..8].concat(), h[8..10].concat(), h[10..16].concat())
    }
    pub fn id(&mut self) -> String {
        let (a, c) = (self.next(), self.next());
        format!("{:016x}{:016x}", a, c)
    }
}
fn random_seed() -> u64 {
    use std::io::Read;
    let mut b = [0u8; 16];
    let ok = std::fs::File::open("/dev/urandom").and_then(|mut f| f.read_exact(&mut b)).is_ok();
    let mut s = if ok {
        u64::from_le_bytes(b[..8].try_into().unwrap()) ^ u64::from_le_bytes(b[8..].try_into().unwrap()).rotate_left(17)
    } else {
        let t = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_nanos()).unwrap_or(0);
        (t as u64) ^ ((t >> 64) as u64) ^ (std::process::id() as u64).rotate_left(32) ^ (&b as *const _ as u64)
    };
    if s == 0 {
        s = 0x9E3779B97F4A7C15
    }
    s
}

// ---------------------------------------------------------------- JSON DOM (fe_json.c: raw strings and numbers)
#[derive(Clone, Debug)]
enum J {
    Null,
    True,
    False,
    Num(Vec<u8>),
    Str(Vec<u8>),
    Arr(Vec<J>),
    Obj(Vec<(Vec<u8>, J)>),
}
impl J {
    fn get(&self, k: &str) -> Option<&J> {
        match self {
            J::Obj(v) => v.iter().find(|(kk, _)| kk == k.as_bytes()).map(|(_, x)| x),
            _ => None,
        }
    }
    fn path(&self, dotted: &str) -> Option<&J> {
        let mut cur = Some(self);
        for t in dotted.split('.').filter(|s| !s.is_empty()) {
            cur = cur.and_then(|c| c.get(t));
        }
        cur
    }
    fn numv(&self) -> f64 {
        match self {
            J::Num(s) => atof(&String::from_utf8_lossy(s)),
            _ => 0.0,
        }
    }
    fn write(&self, w: &mut Vec<u8>, sorted: bool) {
        match self {
            J::Null => w.extend_from_slice(b"null"),
            J::True => w.extend_from_slice(b"true"),
            J::False => w.extend_from_slice(b"false"),
            J::Num(s) => w.extend_from_slice(s),
            J::Str(s) => {
                w.push(b'"');
                w.extend_from_slice(s);
                w.push(b'"')
            }
            J::Arr(a) => {
                w.push(b'[');
                for (i, x) in a.iter().enumerate() {
                    if i > 0 {
                        w.push(b',')
                    }
                    x.write(w, sorted)
                }
                w.push(b']')
            }
            J::Obj(o) => {
                let mut ord: Vec<usize> = (0..o.len()).collect();
                if sorted {
                    // strcmp: up to the first NUL
                    let key = |i: usize| -> &[u8] {
                        let k = &o[i].0;
                        &k[..k.iter().position(|c| *c == 0).unwrap_or(k.len())]
                    };
                    ord.sort_by(|a, b| key(*a).cmp(key(*b)));
                }
                w.push(b'{');
                for (x, i) in ord.iter().enumerate() {
                    if x > 0 {
                        w.push(b',')
                    }
                    w.push(b'"');
                    w.extend_from_slice(&o[*i].0);
                    w.extend_from_slice(b"\":");
                    o[*i].1.write(w, sorted)
                }
                w.push(b'}')
            }
        }
    }
}
fn fnv(s: &[u8]) -> u64 {
    let mut h: u64 = 0xcbf29ce484222325;
    for c in s {
        h ^= *c as u64;
        h = h.wrapping_mul(0x100000001B3);
    }
    h
}
fn jhash(v: &J) -> u64 {
    let mut w = vec![];
    v.write(&mut w, true);
    fnv(&w)
}
struct Pj<'a> {
    b: &'a [u8],
    i: usize,
    depth: i32,
    err: Option<String>,
}
impl<'a> Pj<'a> {
    fn fail(&mut self, m: &str) {
        if self.err.is_none() {
            self.err = Some(format!("{m} at byte 0")); // fe_json.c fail(): its offset expression is always 0
        }
    }
    fn ws(&mut self) {
        while self.i < self.b.len() && matches!(self.b[self.i], b' ' | b'\n' | b'\r' | b'\t') {
            self.i += 1
        }
    }
    fn rawstr(&mut self) -> Option<Vec<u8>> {
        if self.i >= self.b.len() || self.b[self.i] != b'"' {
            self.fail("string expected");
            return None;
        }
        self.i += 1;
        let st = self.i;
        while self.i < self.b.len() && self.b[self.i] != b'"' {
            if self.b[self.i] == b'\\' {
                self.i += 1
            }
            self.i += 1
        }
        if self.i >= self.b.len() {
            self.fail("unterminated string");
            return None;
        }
        let s = self.b[st..self.i].to_vec();
        self.i += 1;
        Some(s)
    }
    fn val(&mut self) -> Option<J> {
        self.ws();
        if self.i >= self.b.len() {
            self.fail("value expected");
            return None;
        }
        self.depth += 1;
        if self.depth > 200 {
            self.fail("too deep");
            return None;
        }
        let c = self.b[self.i];
        let rest = &self.b[self.i..];
        let r = if c == b'{' {
            self.i += 1;
            self.ws();
            let mut o = vec![];
            if self.i < self.b.len() && self.b[self.i] == b'}' {
                self.i += 1;
            } else {
                loop {
                    self.ws();
                    let k = self.rawstr()?;
                    self.ws();
                    if self.i >= self.b.len() || self.b[self.i] != b':' {
                        self.fail("':' expected");
                        return None;
                    }
                    self.i += 1;
                    let x = self.val()?;
                    o.push((k, x));
                    self.ws();
                    if self.i < self.b.len() && self.b[self.i] == b',' {
                        self.i += 1;
                        continue;
                    }
                    if self.i < self.b.len() && self.b[self.i] == b'}' {
                        self.i += 1;
                        break;
                    }
                    self.fail("',' or '}' expected");
                    return None;
                }
            }
            J::Obj(o)
        } else if c == b'[' {
            self.i += 1;
            self.ws();
            let mut a = vec![];
            if self.i < self.b.len() && self.b[self.i] == b']' {
                self.i += 1;
            } else {
                loop {
                    let x = self.val()?;
                    a.push(x);
                    self.ws();
                    if self.i < self.b.len() && self.b[self.i] == b',' {
                        self.i += 1;
                        continue;
                    }
                    if self.i < self.b.len() && self.b[self.i] == b']' {
                        self.i += 1;
                        break;
                    }
                    self.fail("',' or ']' expected");
                    return None;
                }
            }
            J::Arr(a)
        } else if c == b'"' {
            J::Str(self.rawstr()?)
        } else if rest.starts_with(b"true") {
            self.i += 4;
            J::True
        } else if rest.starts_with(b"false") {
            self.i += 5;
            J::False
        } else if rest.starts_with(b"null") {
            self.i += 4;
            J::Null
        } else if c == b'-' || c.is_ascii_digit() {
            let st = self.i;
            while self.i < self.b.len() && b"+-0123456789.eE".contains(&self.b[self.i]) {
                self.i += 1
            }
            J::Num(self.b[st..self.i].to_vec())
        } else {
            self.fail("bad value");
            return None;
        };
        self.depth -= 1;
        Some(r)
    }
}
fn jparse(text: &[u8]) -> Result<J, String> {
    let mut p = Pj { b: text, i: 0, depth: 0, err: None };
    if text.starts_with(&[0xEF, 0xBB, 0xBF]) {
        p.i = 3
    }
    match p.val() {
        Some(v) => {
            p.ws();
            if p.i != text.len() {
                p.fail("trailing data");
                return Err(p.err.unwrap());
            }
            Ok(v)
        }
        None => Err(p.err.unwrap_or_default()),
    }
}
/// fe_comp: the Data of the component of that Type
fn fe_comp(comps: Option<&J>, ty: i32) -> Option<&J> {
    if let Some(J::Arr(a)) = comps {
        for c in a {
            if let Some(t) = c.get("Type") {
                if t.numv() as i32 == ty {
                    return c.get("Data");
                }
            }
        }
    }
    None
}
fn fe_progress_sig(cl: &J) -> String {
    let c = cl.path("unit.components");
    let (st, sk, eq) = (fe_comp(c, 0), fe_comp(c, 8), fe_comp(c, 6));
    let g = |o: Option<&J>, k: &str| o.and_then(|o| o.get(k)).cloned().unwrap_or(J::Null);
    let mut sig = vec![];
    sig.push((b"prog".to_vec(), g(st, "ProgressionStats")));
    sig.push((b"gen".to_vec(), g(st, "GeneratedStats")));
    sig.push((b"perm".to_vec(), g(st, "PermanentStats")));
    sig.push((b"skills".to_vec(), J::Arr(["BasicSkills", "ActiveSkills", "PassiveSkills"].iter().map(|n| g(sk, n)).collect())));
    sig.push((b"trees".to_vec(), g(cl.path("unit.pusd"), "SkillTrees")));
    let mut keys = vec![];
    if let Some(J::Arr(sl)) = eq.and_then(|e| e.get("ItemSlots")) {
        for x in sl {
            keys.push(g(Some(x), "Key"))
        }
    }
    sig.push((b"equip".to_vec(), J::Arr(keys)));
    format!("{:016x}", jhash(&J::Obj(sig)))
}

// ---------------------------------------------------------------- the decoded link (wk_build)
/// a decoded unit with the C decoder's types (pvp_wiki.h wk_build): indices into the link tables, f32 values
#[derive(Debug, Clone, PartialEq)]
pub struct WireBuild {
    pub race: i32,
    pub bg: i32,
    pub lvl: i32,
    pub tal: [i32; 6],
    pub offer: i32,
    pub roll: i32,
    pub advanced: bool,
    pub add_shield: bool,
    pub hide_head: bool,
    pub inds: Vec<i32>,
    pub trees: Vec<i32>,
    pub learned: Vec<i32>,
    pub insc: Vec<i32>,
    pub eq: [i32; WK_SLOTS],
    pub tags: Vec<Vec<(i32, f32)>>,
    /// raw bytes, C string (cut at the first NUL)
    pub name: Vec<u8>,
    pub portrait: i32,
    pub man: Vec<(i32, f32)>,
    pub has_mb: bool,
    pub mb: [f32; 6],
    pub pstk: Vec<(i32, i32)>,
    pub souls: Vec<i32>,
    pub saved_inds: Vec<i32>,
    pub saved_insc: Vec<i32>,
    pub acq: Vec<i32>,
    pub has_elix: bool,
    pub elix: [i32; 6],
    pub ncond: usize,
    pub cond: [i32; 8],
}
impl Default for WireBuild {
    fn default() -> Self {
        WireBuild {
            race: 0,
            bg: -1,
            lvl: 0,
            tal: [0; 6],
            offer: 0,
            roll: 0,
            advanced: false,
            add_shield: false,
            hide_head: false,
            inds: vec![],
            trees: vec![],
            learned: vec![],
            insc: vec![],
            eq: [-1; WK_SLOTS],
            tags: vec![vec![]; WK_SLOTS],
            name: vec![],
            portrait: -1,
            man: vec![],
            has_mb: false,
            mb: [0.0; 6],
            pstk: vec![],
            souls: vec![],
            saved_inds: vec![],
            saved_insc: vec![],
            acq: vec![],
            has_elix: false,
            elix: [0; 6],
            ncond: 0,
            cond: [0; 8],
        }
    }
}

struct Rd<'a> {
    b: &'a [u8],
    i: usize,
    over: bool,
}
impl<'a> Rd<'a> {
    fn ru(&mut self) -> u32 {
        let mut v: u32 = 0;
        let mut sh: u32 = 0;
        loop {
            let x = if self.i < self.b.len() { self.b[self.i] } else { 0 };
            if self.i >= self.b.len() {
                self.over = true
            }
            self.i += 1;
            if sh < 32 {
                v |= ((x & 0x7f) as u32) << sh
            }
            sh += 7;
            if !((x & 0x80) != 0 && sh < 64) {
                break;
            }
        }
        v
    }
    fn rbyte(&mut self) -> u32 {
        if self.i >= self.b.len() {
            self.over = true;
            self.i += 1;
            return 0;
        }
        self.i += 1;
        self.b[self.i - 1] as u32
    }
    fn rzz(&mut self) -> i32 {
        let u = self.ru();
        if u & 1 == 1 {
            ((u.wrapping_add(1) >> 1) as i32).wrapping_neg()
        } else {
            (u >> 1) as i32
        }
    }
}
struct Errs(Option<String>);
impl Errs {
    fn fail(&mut self, m: String) {
        if self.0.is_none() {
            self.0 = Some(m)
        }
    }
}
fn read_list(r: &mut Rd, e: &mut Errs, table_n: usize, what: &str) -> Vec<i32> {
    let n = r.ru();
    let mut out = vec![];
    if n > WK_MAX_LIST {
        e.fail(format!("{what}: {n} entries (max {WK_MAX_LIST})"));
        return out;
    }
    for _ in 0..n {
        let v = r.ru();
        if v as usize >= table_n || (v as i32) < 0 {
            e.fail(format!("{what}: index {v} outside the table ({table_n})"));
            return out;
        }
        out.push(v as i32);
    }
    out
}
fn dec_build(r: &mut Rd, e: &mut Errs) -> Option<WireBuild> {
    let t = tables();
    let mut b = WireBuild::default();
    let race = r.ru();
    if race as usize >= t.race.len() || (race as i32) < 0 {
        e.fail(format!("race index {race} outside the table"));
        return None;
    }
    // as builder.js dsbDecBuild: a retired race comes back as Human
    let ret = &crate::data::dsb().retired;
    b.race = if ret.races.contains_key(&t.race[race as usize]) {
        t.race.iter().position(|k| k == "AFFECTER_Human").unwrap_or(race as usize) as i32
    } else {
        race as i32
    };
    let bg = r.ru();
    if bg != 0 {
        if (bg as i32).wrapping_sub(1) >= t.bg.len() as i32 || (bg as i32) < 0 {
            e.fail(format!("background index {} outside the table", bg.wrapping_sub(1)));
            return None;
        }
        b.bg = bg as i32 - 1;
    }
    b.lvl = r.ru() as i32;
    if b.lvl < 1 || b.lvl > 255 {
        e.fail(format!("level {}", b.lvl));
        return None;
    }
    let mut tp = r.ru();
    let mut t6 = [0i32; 6];
    for i in (0..6).rev() {
        t6[i] = (tp % 5) as i32;
        tp /= 5;
    }
    for i in 0..6 {
        b.tal[i] = if (t6[i] as usize) < t.tier.len() { t6[i] } else { 1 };
    }
    let fl = r.ru();
    b.offer = (fl & 3) as i32;
    b.roll = ((fl >> 2) & 3) as i32;
    b.advanced = (fl >> 4) & 1 == 1;
    b.add_shield = (fl >> 5) & 1 == 1;
    b.hide_head = (fl >> 15) & 1 == 1;
    if b.offer > 2 {
        b.offer = 0
    }
    if b.roll > 2 {
        b.roll = 1
    }
    let bit = |n: u32| (fl >> n) & 1 == 1;
    // retired traits (the designers' test rows, D.retired) are dropped, as builder.js dsbDecBuild dropGone
    let drop_retired = |v: Vec<i32>| -> Vec<i32> { v.into_iter().filter(|i| !ret.ind.contains_key(&t.ind[*i as usize])).collect() };
    b.inds = drop_retired(read_list(r, e, t.ind.len(), "traits"));
    b.trees = read_list(r, e, t.tree.len(), "skill trees");
    b.learned = read_list(r, e, t.skill.len(), "skills");
    b.insc = read_list(r, e, t.insc.len(), "inscriptions");
    if e.0.is_some() {
        return None;
    }
    let nslot = t.slot.len().min(WK_SLOTS);
    let eqmask = r.ru();
    for s in 0..nslot {
        if eqmask >> s & 1 == 1 {
            let it = r.ru();
            if it as usize >= t.item.len() || (it as i32) < 0 {
                e.fail(format!("item index {it} outside the table"));
                return None;
            }
            b.eq[s] = it as i32;
        }
    }
    let tagmask = r.ru();
    for s in 0..nslot {
        if tagmask >> s & 1 == 1 {
            let n = r.ru();
            if n > 8 {
                e.fail(format!("slot {}: {} tags", t.slot[s], n));
                return None;
            }
            for _ in 0..n {
                let idx = r.ru() as i32;
                let frac = r.rbyte();
                let it = b.eq[s];
                if it < 0 || idx < 0 || idx as usize >= t.item_tags[it as usize].len() {
                    continue; // as the browser: a tag the item lacks is skipped
                }
                let (lo, hi) = t.item_tags[it as usize][idx as usize];
                b.tags[s].push((idx, (lo + (frac as f64 / 255.0) * (hi - lo)) as f32));
            }
        }
    }
    if bit(6) {
        let len = r.ru();
        if len > 48 {
            e.fail(format!("name of {len} bytes"));
            return None;
        }
        let mut nm = vec![];
        for _ in 0..len {
            nm.push(r.rbyte() as u8)
        }
        if let Some(z) = nm.iter().position(|c| *c == 0) {
            nm.truncate(z)
        }
        for c in nm.iter_mut() {
            if *c < 32 || *c == b'"' || *c == b'\\' {
                *c = b' '
            }
        }
        b.name = nm;
    }
    if bit(7) {
        let pi = r.ru() as i32;
        let pn = t.portraits[b.race as usize].as_ref().map(|p| p.len()).unwrap_or(0) as i32;
        if pi < pn {
            b.portrait = if pi < 0 { -1 } else { pi } // a negative index is "none" for every reader
        }
    }
    if bit(8) {
        let n = r.ru();
        if n > WK_MAX_LIST {
            e.fail(format!("{n} stat overrides"));
            return None;
        }
        for _ in 0..n {
            let si = r.ru() as i32;
            let v = r.rzz() as f32 / 100.0f32;
            if si >= 0 && (si as usize) < t.stat.len() {
                b.man.push((si, v))
            }
        }
    }
    if bit(9) {
        b.has_mb = true;
        for k in 0..6 {
            b.mb[k] = r.rzz() as f32 / 100.0f32
        }
    }
    if bit(10) {
        let n = r.ru();
        if n > WK_MAX_LIST {
            e.fail(format!("{n} passive stacks"));
            return None;
        }
        for _ in 0..n {
            let sk = r.ru() as i32;
            let st = r.ru() as i32;
            if sk >= 0 && (sk as usize) < t.skill.len() {
                b.pstk.push((sk, st))
            }
        }
    }
    if bit(11) {
        b.souls = read_list(r, e, t.soul.len(), "souls")
    }
    if bit(12) {
        b.saved_inds = drop_retired(read_list(r, e, t.ind.len(), "saved traits"))
    }
    if bit(13) {
        b.saved_insc = read_list(r, e, t.insc.len(), "saved inscriptions")
    }
    if bit(14) {
        b.acq = read_list(r, e, t.acq.len(), "acquired traits")
    }
    if bit(16) {
        b.has_elix = true;
        let n = r.ru();
        for k in 0..n {
            let u = r.ru() as i32;
            if k < 6 {
                b.elix[k as usize] = u
            }
            if r.over {
                break; // the link is refused as cut short anyway
            }
        }
    }
    if bit(17) {
        let n = r.ru();
        let cn = t.cond.len();
        for k in 0..n as usize {
            let ci = r.ru() as i32;
            if k < 8 && k < cn && ci > 0 && ci as usize <= t.cond[k].len() {
                b.cond[k] = ci
            }
            if r.over {
                break;
            }
        }
        b.ncond = cn.min(8);
    }
    if e.0.is_some() {
        None
    } else {
        Some(b)
    }
}
fn parse_blob(by: &[u8], e: &mut Errs) -> Option<(Vec<WireBuild>, bool)> {
    if by.len() < 4 {
        e.fail("link too short".into());
        return None;
    }
    let mut r = Rd { b: by, i: 0, over: false };
    let ver = r.rbyte();
    if ver != 1 && ver != 2 {
        e.fail(format!("unknown link version {ver}"));
        return None;
    }
    let mut fp = r.rbyte() << 8;
    fp |= r.rbyte();
    let cnt = r.ru();
    if cnt < 1 || cnt as usize > WK_MAX_UNITS {
        e.fail(format!("{cnt} units in the link (1 to {WK_MAX_UNITS})"));
        return None;
    }
    let mut out = vec![];
    for _ in 0..cnt {
        out.push(dec_build(&mut r, e)?);
    }
    if r.over || r.i > by.len() {
        e.fail("link cut short (corrupted or truncated)".into());
        return None;
    }
    Some((out, fp != tables().fp))
}
fn b64v(c: u8) -> Option<u32> {
    Some(match c {
        b'A'..=b'Z' => c - b'A',
        b'a'..=b'z' => c - b'a' + 26,
        b'0'..=b'9' => c - b'0' + 52,
        b'-' | b'+' => 62,
        b'_' | b'/' => 63,
        _ => return None,
    } as u32)
}
fn b64url_dec_c(s: &[u8]) -> Option<Vec<u8>> {
    let mut o = vec![];
    let (mut acc, mut bits) = (0u32, 0);
    for c in s {
        if *c == b'=' {
            break;
        }
        acc = acc << 6 | b64v(*c)?;
        bits += 6;
        if bits >= 8 {
            bits -= 8;
            o.push((acc >> bits) as u8)
        }
    }
    Some(o)
}
/// inflateInit2(-15) into a buffer of n*8+1024 bytes doubled up to 1 MiB; needs Z_STREAM_END
fn inflate_raw(input: &[u8]) -> Option<Vec<u8>> {
    use flate2::{Decompress, FlushDecompress, Status};
    let mut cap = input.len() * 8 + 1024;
    let mut d = Decompress::new(false);
    let mut out: Vec<u8> = Vec::with_capacity(cap);
    loop {
        if out.len() >= cap {
            cap *= 2;
            if cap > (1 << 20) {
                return None;
            }
        }
        out.reserve(cap - out.len());
        let before_in = d.total_in();
        let before_out = d.total_out();
        let st = {
            let spare = cap - out.len();
            let mut buf = vec![0u8; spare];
            let pos = (d.total_in() as usize).min(input.len());
            let st = d.decompress(&input[pos..], &mut buf, FlushDecompress::None).ok()?;
            let got = (d.total_out() - before_out) as usize;
            out.extend_from_slice(&buf[..got]);
            st
        };
        match st {
            Status::StreamEnd => return Some(out),
            Status::Ok => {
                if d.total_in() == before_in && d.total_out() == before_out {
                    return None; // no progress = Z_BUF_ERROR
                }
            }
            Status::BufError => return None,
        }
    }
}
/// wk_decode: the units of a team link (`...team=CODE` or the bare CODE), C rules. Ok((units, fingerprint mismatch))
pub fn decode_wire(text: &str) -> Result<(Vec<WireBuild>, bool), String> {
    let tb = text.as_bytes();
    let s = match tb.windows(5).position(|w| w == b"team=") {
        Some(p) => &tb[p + 5..],
        None => tb,
    };
    let mut i = 0;
    while i < s.len() && matches!(s[i], b' ' | b'\t' | b'\n') {
        i += 1
    }
    let s = &s[i..];
    let n = s.iter().position(|c| matches!(c, 0 | b'&' | b'#' | b' ' | b'\n' | b'\r')).unwrap_or(s.len());
    if n == 0 {
        return Err("no team code in the link".into());
    }
    let by = b64url_dec_c(&s[..n]).ok_or_else(|| "the team code is not base64url".to_string())?;
    let mut e1 = Errs(None);
    if let Some(k) = parse_blob(&by, &mut e1) {
        return Ok(k);
    }
    match inflate_raw(&by) {
        Some(raw) => {
            let mut e = Errs(None);
            parse_blob(&raw, &mut e).ok_or_else(|| e.0.unwrap_or_default())
        }
        None => Err(e1.0.filter(|x| !x.is_empty()).unwrap_or_else(|| "not a team link".into())),
    }
}

/// wk_json(b, 1): the decoded build as JSON with key names (builder.js dsbDecBuild shape)
fn wk_json(x: &WireBuild) -> Vec<u8> {
    let t = tables();
    let mut b: Vec<u8> = vec![];
    macro_rules! w { ($($a:tt)*) => { b.extend_from_slice(format!($($a)*).as_bytes()) } }
    fn slist(b: &mut Vec<u8>, key: &str, v: &[i32], tab: &[String]) {
        b.extend_from_slice(format!(",\"{key}\":[").as_bytes());
        for (i, k) in v.iter().enumerate() {
            b.extend_from_slice(format!("{}\"{}\"", if i > 0 { "," } else { "" }, tab[*k as usize]).as_bytes());
        }
        b.push(b']');
    }
    const OFFER: [&str; 3] = ["Least", "Ordinary", "Plentiful"];
    const ROLL: [&str; 3] = ["min", "avg", "max"];
    let tf = |v: bool| if v { "true" } else { "false" };
    w!("[{{\"race\":\"{}\",\"bg\":", t.race[x.race as usize]);
    if x.bg >= 0 {
        w!("\"{}\"", t.bg[x.bg as usize])
    } else {
        w!("null")
    }
    w!(",\"lvl\":{},\"tal\":{{", x.lvl);
    for i in 0..6 {
        w!("{}\"{}\":\"{}\"", if i > 0 { "," } else { "" }, t.maj[i], t.tier[x.tal[i] as usize]);
    }
    w!(
        "}},\"offer\":\"{}\",\"roll\":\"{}\",\"advanced\":{},\"addShield\":{},\"hideHead\":{}",
        OFFER[x.offer as usize],
        ROLL[x.roll as usize],
        tf(x.advanced),
        tf(x.add_shield),
        tf(x.hide_head)
    );
    slist(&mut b, "inds", &x.inds, &t.ind);
    slist(&mut b, "insctrees", &x.trees, &t.tree);
    slist(&mut b, "learned", &x.learned, &t.skill);
    slist(&mut b, "insc", &x.insc, &t.insc);
    w!(",\"eq\":{{");
    let nslot = t.slot.len().min(WK_SLOTS);
    let mut f = 0;
    for s in 0..nslot {
        if x.eq[s] >= 0 {
            w!("{}\"{}\":\"{}\"", if f > 0 { "," } else { "" }, t.slot[s], t.item[x.eq[s] as usize]);
            f += 1;
        }
    }
    w!("}},\"tags\":{{");
    f = 0;
    for s in 0..nslot {
        if !x.tags[s].is_empty() {
            w!("{}\"{}\":[", if f > 0 { "," } else { "" }, t.slot[s]);
            f += 1;
            for (k, (i, v)) in x.tags[s].iter().enumerate() {
                w!("{}{{\"i\":{},\"v\":{}}}", if k > 0 { "," } else { "" }, i, fmt_f(*v as f64, 4));
            }
            w!("]");
        }
    }
    w!("}},\"manual\":{{");
    for (k, (st, v)) in x.man.iter().enumerate() {
        w!("{}\"{}\":{}", if k > 0 { "," } else { "" }, t.stat[*st as usize], fmt_f(*v as f64, 2));
    }
    w!("}}");
    if !x.name.is_empty() {
        w!(",\"name\":\"");
        b.extend_from_slice(&x.name);
        w!("\"");
    }
    if x.portrait >= 0 {
        if let Some(p) = &t.portraits[x.race as usize] {
            w!(",\"portrait\":\"{}\"", p[x.portrait as usize]);
        }
    }
    if x.has_mb {
        w!(",\"majorBase\":{{");
        for k in 0..6 {
            w!("{}\"{}\":{}", if k > 0 { "," } else { "" }, t.maj[k], fmt_f(x.mb[k] as f64, 2));
        }
        w!("}}");
    }
    if !x.pstk.is_empty() {
        w!(",\"passiveStacks\":{{");
        for (k, (sk, st)) in x.pstk.iter().enumerate() {
            w!("{}\"{}\":{}", if k > 0 { "," } else { "" }, t.skill[*sk as usize], st);
        }
        w!("}}");
    }
    if !x.souls.is_empty() {
        slist(&mut b, "souls", &x.souls, &t.soul)
    }
    if !x.saved_inds.is_empty() {
        slist(&mut b, "savedInds", &x.saved_inds, &t.ind)
    }
    if !x.saved_insc.is_empty() {
        slist(&mut b, "savedInsc", &x.saved_insc, &t.insc)
    }
    if !x.acq.is_empty() {
        slist(&mut b, "acqInds", &x.acq, &t.acq)
    }
    if x.has_elix {
        w!(",\"elix\":{{");
        let mut g = 0;
        for k in 0..6 {
            if x.elix[k] > 0 {
                w!("{}\"{}\":{}", if g > 0 { "," } else { "" }, t.maj[k], x.elix[k]);
                g += 1;
            }
        }
        w!("}}");
    }
    if x.ncond > 0 {
        w!(",\"cond\":[");
        let mut g = 0;
        for k in 0..x.ncond {
            if x.cond[k] > 0 {
                w!("{}\"{}\"", if g > 0 { "," } else { "" }, t.cond[k][(x.cond[k] - 1) as usize]);
                g += 1;
            }
        }
        w!("]");
    }
    w!("}}]");
    b
}
/// wk_hash(b, 1)
pub fn wk_hash(x: &WireBuild) -> u64 {
    let mut h: u64 = 1469598103934665603;
    for c in wk_json(x) {
        h ^= c as u64;
        h = h.wrapping_mul(1099511628211);
    }
    h
}

// ---------------------------------------------------------------- pvp_build.c
#[derive(Clone)]
struct StatV {
    st: String,
    flat: f64,
    pct: f64,
}
#[derive(Default)]
struct Stats {
    s: Vec<StatV>,
}
fn trunc(s: &str, n: usize) -> &str {
    if s.len() <= n {
        s
    } else {
        let mut k = n;
        while !s.is_char_boundary(k) {
            k -= 1
        }
        &s[..k]
    }
}
impl Stats {
    fn sget(&mut self, st: &str) -> Option<&mut StatV> {
        if let Some(i) = self.s.iter().position(|v| v.st == st) {
            return Some(&mut self.s[i]);
        }
        if self.s.len() >= 64 {
            return None;
        }
        self.s.push(StatV { st: trunc(st, 47).to_string(), flat: 0.0, pct: 0.0 });
        self.s.last_mut()
    }
    fn sadd(&mut self, st: &str, v: f64) {
        let n = st.len();
        if n > 11 && st.ends_with("PerModifier") {
            let base = trunc(&st[..n - 11], 47).to_string();
            if let Some(x) = self.sget(&base) {
                x.pct += v
            }
        } else if let Some(x) = self.sget(st) {
            x.flat += v
        }
    }
    fn sadd_list(&mut self, adj: Option<&str>) {
        let buf = trunc(adj.unwrap_or(""), 399);
        for tk in buf.split(';').filter(|x| !x.is_empty()) {
            let Some((k, v)) = tk.split_once('=') else { continue };
            self.sadd(k, atof(v));
        }
    }
    fn sflat(&self, st: &str) -> f64 {
        self.s.iter().find(|v| v.st == st).map(|v| v.flat).unwrap_or(0.0)
    }
    fn spct(&self, st: &str) -> f64 {
        self.s.iter().find(|v| v.st == st).map(|v| v.pct).unwrap_or(0.0)
    }
}
struct Keys(Vec<String>);
impl Keys {
    fn add(&mut self, k: Option<&str>) {
        let Some(k) = k.filter(|k| !k.is_empty()) else { return };
        if self.0.iter().any(|x| x == k) {
            return;
        }
        if self.0.len() < 160 {
            self.0.push(k.to_string())
        }
    }
    fn has(&self, k: &str) -> bool {
        self.0.iter().any(|x| x == k)
    }
}
fn has_tree(tr: &[i32], t: i32) -> bool {
    tr.contains(&t)
}
fn unit_trees(b: &WireBuild) -> Vec<i32> {
    let t = tables();
    let mut tr = vec![];
    if b.bg >= 0 {
        for x in t.bg_mains[b.bg as usize].iter().take(4) {
            if *x >= 0 {
                tr.push(*x)
            }
        }
        for x in t.bg_subs[b.bg as usize].iter().take(4) {
            if *x >= 0 {
                tr.push(*x)
            }
        }
    }
    for x in &b.trees {
        if tr.len() >= 64 {
            break;
        }
        if !has_tree(&tr, *x) {
            tr.push(*x)
        }
    }
    tr
}
fn unit_affecters(b: &WireBuild) -> Keys {
    let t = tables();
    let mut k = Keys(vec![]);
    k.add(Some(&t.race[b.race as usize]));
    if b.bg >= 0 {
        k.add(Some(&t.bg[b.bg as usize]))
    }
    if !b.saved_inds.is_empty() {
        for i in &b.saved_inds {
            k.add(Some(&t.ind[*i as usize]))
        }
    } else {
        for i in &b.inds {
            k.add(Some(&t.ind[*i as usize]))
        }
    }
    for i in &b.trees {
        k.add(t.tree_insc[*i as usize].as_deref())
    }
    for i in &b.insc {
        k.add(Some(&t.insc[*i as usize]))
    }
    for i in &b.saved_insc {
        k.add(Some(&t.insc[*i as usize]))
    }
    for i in &b.souls {
        k.add(Some(&t.soul[*i as usize]))
    }
    for i in &b.acq {
        k.add(Some(&t.acq[*i as usize]))
    }
    k
}
fn nslot() -> usize {
    tables().slot.len().min(WK_SLOTS)
}
fn unit_stats(b: &WireBuild, k: &Keys) -> Stats {
    let t = tables();
    let mut s = Stats::default();
    for a in &k.0 {
        if let Some(adj) = t.aff_adj.get(a) {
            s.sadd_list(Some(adj))
        }
    }
    for sl in 0..nslot() {
        let it = b.eq[sl];
        if it < 0 || it as usize >= t.item.len() {
            continue;
        }
        let item = &t.items[it as usize];
        s.sadd_list(item.adj.as_deref());
        for a in trunc(&item.onequip, 299).split(';').filter(|x| !x.is_empty()) {
            if let Some(adj) = t.aff_adj.get(a) {
                s.sadd_list(Some(adj))
            }
        }
        for (idx, v) in &b.tags[sl] {
            if (*idx as usize) < item.tag_stat.len() {
                s.sadd(&item.tag_stat[*idx as usize], *v as f64)
            }
        }
    }
    s
}
struct Majors {
    gen: [f64; 6],
    prog: [f64; 6],
    perm: [f64; 6],
    talent: [i32; 6],
}
fn calc_majors(b: &WireBuild, k: &Keys, s: &Stats) -> Majors {
    let t = tables();
    let mut m = Majors { gen: [0.0; 6], prog: [0.0; 6], perm: [0.0; 6], talent: [0; 6] };
    let q: f64 = if b.roll == 0 { 0.0 } else if b.roll == 2 { 1.0 } else { 0.5 };
    let bump = k.has("AFFECTER_PerfectionInscription") as i32;
    let f: f32 = t.offer[if b.offer < 0 || b.offer > 2 { 0 } else { b.offer as usize }];
    for i in 0..6 {
        m.talent[i] = b.tal[i] - 1;
        if b.has_elix && b.elix[i] > 0 {
            m.perm[i] = b.elix[i] as f64 * t.elixir[i] as f64
        }
        if b.has_mb {
            m.gen[i] = b.mb[i] as f64;
            continue;
        }
        let b0: f32 = t.major_base[i];
        let rr0 = t.race_roll[b.race as usize][i];
        let rr: f32 = rr0[0] + (((rr0[1] - rr0[0]) as f64 * q) as f32);
        let br: f32 = if b.bg >= 0 {
            let x = t.bg_roll[b.bg as usize][i];
            x[0] + (((x[1] - x[0]) as f64 * q) as f32)
        } else {
            0.0
        };
        m.gen[i] = ((((rr + br) / 100.0f32 + 1.0f32) * b0).trunc() - b0) as f64; // float subtraction, as C
        let tier = (b.tal[i] + bump).min(4) as usize;
        let lo = t.talent[tier][i][0] as i32;
        let hi = t.talent[tier][i][1] as i32;
        let per: f64 = if b.roll == 0 {
            rhe((lo as f32 * f) as f64)
        } else if b.roll == 2 {
            rhe((hi as f32 * f) as f64)
        } else {
            let mut p = 0.0;
            let mut kk = lo;
            while kk <= hi {
                p += rhe((kk as f32 * f) as f64);
                kk += 1;
            }
            p / (hi - lo + 1) as f64
        };
        m.prog[i] = per * (b.lvl - 1) as f64;
    }
    for (st, v) in &b.man {
        for i in 0..6 {
            if t.stat[*st as usize] == t.maj[i] {
                let raw = *v as f64 / (1.0 + s.spct(&t.maj[i]) / 100.0);
                m.gen[i] = raw - t.major_base[i] as f64 - s.sflat(&t.maj[i]) - m.perm[i];
                m.prog[i] = 0.0;
            }
        }
    }
    m
}
fn learned(b: &WireBuild, sk: i32) -> bool {
    b.learned.contains(&sk)
}
fn pstk_of(b: &WireBuild, sk: i32) -> i32 {
    b.pstk.iter().find(|p| p.0 == sk).map(|p| p.1).unwrap_or(-1)
}
fn milestone_count(b: &WireBuild, sk: usize, tr: &[i32]) -> i32 {
    let t = tables();
    let Some(g) = &t.pg[sk] else { return 0 };
    if g.aff.is_none() || g.thr.is_empty() || t.sk_tree[sk] < 0 || !has_tree(tr, t.sk_tree[sk]) {
        return 0;
    }
    let st = pstk_of(b, sk as i32);
    if st >= 0 {
        return st;
    }
    if b.has_mb {
        return 0;
    }
    if learned(b, sk as i32) {
        *g.thr.last().unwrap()
    } else {
        0
    }
}
fn passive_grants(b: &WireBuild) -> Stats {
    let t = tables();
    let mut p = Stats::default();
    for sk in &b.learned {
        let Some(g) = &t.pg[*sk as usize] else { continue };
        if !g.cond || !g.thr.is_empty() {
            continue;
        }
        let mut mul = 1.0f64;
        if b.has_mb {
            let st = pstk_of(b, *sk);
            if st < 0 {
                continue;
            }
            mul = 0.0;
            for th in &g.thr {
                if st >= *th {
                    mul += 1.0
                }
            }
        }
        for tk in trunc(&g.adj, 399).split(';').filter(|x| !x.is_empty()) {
            let Some((k, v)) = tk.split_once('=') else { continue };
            let val = if g.per >= 0.0 && b.has_mb { g.per as f64 } else { atof(v) };
            if let Some(x) = p.sget(k) {
                x.flat += val * mul
            }
        }
    }
    p
}
fn add_grants(s: &mut Stats, p: &Stats) {
    for v in &p.s {
        s.sadd(&v.st, v.flat)
    }
}
/// pb_stored_majors: the six majors the save holds as base + Generated + Progression + Permanent
pub fn stored_majors(b: &WireBuild) -> [f64; 6] {
    let t = tables();
    let k = unit_affecters(b);
    let mut s = unit_stats(b, &k);
    let p = passive_grants(b);
    add_grants(&mut s, &p);
    let m = calc_majors(b, &k, &s);
    let mut out = [0.0; 6];
    for i in 0..6 {
        let v = p.s.iter().filter(|x| x.st == t.maj[i]).last().map(|x| x.flat).unwrap_or(0.0);
        out[i] = t.major_base[i] as f64 + m.gen[i] + m.prog[i] + m.perm[i] + v;
    }
    out
}

/// note(): one line, the buffer of pvp_units.c (600 bytes) as the limit
fn note(notes: &mut Vec<u8>, line: String) {
    const N: usize = 600;
    let at = notes.len();
    if at + 2 >= N {
        return;
    }
    let lb = line.as_bytes();
    let room = N - at - 1;
    notes.extend_from_slice(&lb[..lb.len().min(room)]);
    if notes.len() + 1 < N {
        notes.push(b'\n')
    }
}
fn sq(o: &mut Vec<u8>, s: &[u8]) {
    o.push(b'"');
    for c in s {
        if *c == b'"' || *c == b'\\' {
            o.push(b'\\');
            o.push(*c)
        } else if *c < 0x20 {
            o.extend_from_slice(format!("\\u{:04x}", c).as_bytes())
        } else {
            o.push(*c)
        }
    }
    o.push(b'"');
}
fn empty_slot(o: &mut Vec<u8>) {
    o.extend_from_slice(format!("{{\"Guid\":\"{ZERO}\",\"Key\":\"\",\"Amount\":0,\"AutoFixed\":false}}").as_bytes());
}
const FLAT_ITEM_STATS: [&str; 11] = [
    "CriticalChance",
    "CriticalDamageBonus",
    "CooldownReduction",
    "HitChance",
    "DodgeChance",
    "GuardChance",
    "GuardDamageReduction",
    "PhysicalResistance",
    "MagicResistance",
    "MovementSpeed",
    "HealthPercentageRegeneration",
];

/// one unit written as a save closure + manifest
#[derive(Debug, Clone)]
pub struct Entry {
    /// the compact entry `{"closure":...,"manifest":...}` (jdump(e, 0))
    pub json: String,
    /// what the save cannot carry as the builder shows it, one line each
    pub notes: Vec<String>,
    pub unit_guid: String,
    /// manifest.name_key (raw JSON string text)
    pub name_key: String,
    pub closure_hash: String,
    pub progress_sig: String,
    pub export_id: String,
}

/// pb_entry: Err = the refusal text (the game cannot hold what the link asks for)
pub fn pb_entry(b: &WireBuild, player_uid: &str, rng: &mut Rng) -> Result<Entry, String> {
    let t = tables();
    let nsl = nslot();
    let mut notes: Vec<u8> = vec![];
    let race_ok = b.race >= 0 && (b.race as usize) < t.race.len();
    if !race_ok || t.prof[b.race as usize].is_empty() {
        let rk = if race_ok { t.race[b.race as usize].as_str() } else { "" };
        return Err(format!("race {rk} has no look (portraits) in this game build"));
    }
    if b.lvl < 1 || b.lvl as f64 > t.max_level {
        return Err(format!("level {} is outside 1..{}", b.lvl, t.max_level as i32));
    }
    if b.tal.iter().any(|x| *x < 0 || *x > 4) {
        return Err("talent tier out of range".into());
    }
    let k = unit_affecters(b);
    let mut s = unit_stats(b, &k);
    let pg = passive_grants(b);
    add_grants(&mut s, &pg);
    let m = calc_majors(b, &k, &s);
    let tr = unit_trees(b);
    for sl in 0..nsl {
        let it = b.eq[sl];
        if it < 0 {
            continue;
        }
        if it as usize >= t.item.len() || t.items[it as usize].slot != sl as i32 {
            let ik = if (it as usize) < t.item.len() { t.item[it as usize].as_str() } else { "" };
            return Err(format!("{} cannot go in the {} slot", ik, t.slot[sl]));
        }
        let item = &t.items[it as usize];
        for (ix, v) in &b.tags[sl] {
            if *ix as usize >= item.tag_stat.len() {
                return Err(format!("{}: bonus {} is not in its pool", t.item[it as usize], ix));
            }
            let lh = item.tag_lohi[*ix as usize];
            let v = *v as f64;
            if v < lh[0] as f64 - 1e-3 || v > lh[1] as f64 + 1e-3 {
                return Err(format!(
                    "{}: {} {} is outside {}..{}",
                    t.item[it as usize],
                    item.tag_stat[*ix as usize],
                    fmt_f(v, 2),
                    fmt_f(lh[0] as f64, 2),
                    fmt_f(lh[1] as f64, 2)
                ));
            }
        }
        if b.tags[sl].len() as i32 > item.tagamount {
            note(&mut notes, format!("{} carries {} bonus(es); the game rolls {}", t.item[it as usize], b.tags[sl].len(), item.tagamount));
        }
    }
    let mut used = [0f64; 2];
    for sk in &b.learned {
        let sk = *sk as usize;
        let tt = t.sk_tree[sk];
        if tt < 0 || !has_tree(&tr, tt) {
            return Err(format!("{} is not in one of this unit's skill trees", t.skill[sk]));
        }
        let kind = t.tree_kind[tt as usize];
        if kind < 0 {
            return Err(format!("tree {} has no pool", t.tree[tt as usize]));
        }
        let apt = match &t.tree_apt[tt as usize] {
            Some(a) => rhe(s.sflat(a)),
            None => 0.0,
        };
        used[kind as usize] += if kind == 0 { t.sk_sp[sk] as f64 - apt } else { t.sk_sp[sk] as f64 };
    }
    let mut budget = [(t.sp[0] + t.sp[2] * (b.lvl - 1)) as f64, (t.sp[1] + t.sp[3] * (b.lvl - 1)) as f64];
    for tt in &b.trees {
        if t.tree_insc[*tt as usize].is_some() {
            budget[if t.tree_kind[*tt as usize] == 0 { 0 } else { 1 }] += 5.0;
        }
    }
    for kk in 0..2 {
        if used[kk] > budget[kk] + 1e-6 {
            return Err(format!(
                "{} skills cost {} points, the unit has {}",
                if kk == 1 { "sub" } else { "main" },
                fmt_f(used[kk], 0),
                fmt_f(budget[kk], 0)
            ));
        }
    }
    for (st, v) in &b.man {
        let major = t.maj.iter().any(|mj| *mj == t.stat[*st as usize]);
        if !major {
            note(&mut notes, format!("typed {} {}: a save stores no such number (the game computes it)", t.stat[*st as usize], fmt_f(*v as f64, 2)));
        }
    }
    for g in 0..b.ncond.min(8) {
        if b.cond[g] != 0 {
            note(&mut notes, format!("hunger / exhaustion setting {g}: a passing state, not carried"));
        }
    }
    // profile
    let pn = t.prof[b.race as usize].len() as i32;
    let pi = if b.portrait >= 0 && b.portrait < pn { b.portrait as usize } else { 0 };
    let p = &t.prof[b.race as usize][pi];
    let defname = format!("TEXTKEY_PLAYERNAME_{}_{}_0", t.race_name[b.race as usize], if p.gender != 0 { "Female" } else { "Male" });

    let ug = rng.guid();
    let mut ig: Vec<String> = vec![String::new(); WK_SLOTS];
    let mut o: Vec<u8> = vec![];
    macro_rules! w { ($($a:tt)*) => { o.extend_from_slice(format!($($a)*).as_bytes()) } }
    let c = |i: usize| if i > 0 { "," } else { "" };
    w!("{{\"fe_closure\":1,\"unit\":{{\"guid\":\"{ug}\",\"side\":\"Base\",\"components\":[");
    w!("{{\"Type\":0,\"Data\":{{\"$type\":\"Refactor.StatusComponentSaveData, Assembly-CSharp\",\"SchemaVersion\":2,\"StateStats\":{{\"HealthHead\":1.0,\"HealthBody\":1.0,\"Energy\":1.0,\"Hunger\":1.0,\"Stress\":0.0}},\"ProgressionStats\":{{\"Level\":{}.0", b.lvl);
    const TAL: [&str; 6] = ["TalentStrength", "TalentConstitution", "TalentWillPower", "TalentIntelligence", "TalentAgility", "TalentPerception"];
    for i in 0..6 {
        w!(",\"{}\":{}.0", TAL[i], m.talent[i]);
    }
    w!(",\"MainSkillPoint\":{},\"SubSkillPoint\":{},\"Exp\":0.0", fmt_g6(budget[0] - used[0]), fmt_g6(budget[1] - used[1]));
    for i in 0..6 {
        if m.prog[i] != 0.0 {
            w!(",\"{}\":{}", t.maj[i], fmt_g6(m.prog[i]));
        }
    }
    w!("}},\"GeneratedStats\":{{");
    for i in 0..6 {
        w!("{}\"{}\":{}", c(i), t.maj[i], fmt_g6(m.gen[i]));
    }
    w!("}},\"PermanentStats\":{{");
    {
        let mut tt = Stats::default();
        for i in 0..6 {
            if m.perm[i] != 0.0 {
                if let Some(v) = tt.sget(&t.maj[i]) {
                    v.flat += m.perm[i]
                }
            }
        }
        for x in &pg.s {
            if let Some(v) = tt.sget(&x.st) {
                v.flat += x.flat
            }
        }
        let mut kk = 0;
        for x in &tt.s {
            if x.flat != 0.0 {
                w!("{}\"{}\":{}", c(kk), x.st, fmt_g6(x.flat));
                kk += 1;
            }
        }
    }
    w!("}}}}}},");
    w!("{{\"Type\":1,\"Data\":{{\"$type\":\"Refactor.EntityInfoComponentSaveData, Assembly-CSharp\",\"Faction\":1,\"IsDead\":false,\"Option\":0}}}},");
    w!("{{\"Type\":3,\"Data\":{{\"$type\":\"Refactor.AffecterComponentSaveData, Assembly-CSharp\",\"AffecterHolders\":[");
    let mut nh = 0;
    for key in &k.0 {
        w!("{}{{\"Key\":\"{}\",\"Stack\":1,\"Duration\":0.0,\"InitialDuration\":0.0}}", c(nh), key);
        nh += 1;
    }
    for sk in 0..t.skill.len() {
        let n = milestone_count(b, sk, &tr);
        if n <= 0 {
            continue;
        }
        let aff = t.pg[sk].as_ref().and_then(|g| g.aff.clone()).unwrap_or_default();
        if k.has(&aff) {
            continue;
        }
        w!("{}{{\"Key\":\"{}\",\"Stack\":{},\"Duration\":0.0,\"InitialDuration\":0.0}}", c(nh), aff, n);
        nh += 1;
    }
    for (sk, st) in &b.pstk {
        let g = &t.pg[*sk as usize];
        if g.as_ref().map(|g| g.thr.is_empty()).unwrap_or(true) {
            note(&mut notes, format!("{}: stack {} is not a milestone counter in the builder, dropped", t.skill[*sk as usize], st));
        }
    }
    w!("],\"InactiveMoodCauseHolders\":[]}}}},");
    w!("{{\"Type\":4,\"Data\":{{\"$type\":\"Refactor.CombatComponentSaveData, Assembly-CSharp\",\"State\":0}}}},");
    w!("{{\"Type\":5,\"Data\":{{\"$type\":\"Refactor.InventoryComponentSaveData, Assembly-CSharp\",\"ItemSlots\":[");
    for i in 0..36 {
        if i > 0 {
            w!(",")
        }
        empty_slot(&mut o);
    }
    w!("],\"TempSlot\":");
    empty_slot(&mut o);
    w!("}}}},");
    w!("{{\"Type\":6,\"Data\":{{\"$type\":\"Refactor.InventoryComponentSaveData, Assembly-CSharp\",\"ItemSlots\":[");
    for sl in 0..nsl {
        if sl > 0 {
            w!(",")
        }
        if b.eq[sl] < 0 {
            empty_slot(&mut o);
            continue;
        }
        ig[sl] = rng.guid();
        w!("{{\"Guid\":\"{}\",\"Key\":\"{}\",\"Amount\":1,\"AutoFixed\":true}}", ig[sl], t.item[b.eq[sl] as usize]);
    }
    w!("],\"TempSlot\":");
    empty_slot(&mut o);
    w!("}}}},");
    w!("{{\"Type\":7,\"Data\":{{\"$type\":\"Refactor.TaskComponentSaveData, Assembly-CSharp\",\"TaskType\":0,\"TaskGroupGuid\":\"{ZERO}\",\"TaskDataGuid\":\"{ZERO}\",\"TargetString\":null}}}},");
    // skills
    let (mut basic, mut act, mut pas, mut extra): (Vec<String>, Vec<String>, Vec<String>, Vec<String>) = (vec![], vec![], vec![], vec![]);
    fn addu(a: &mut Vec<String>, cap: usize, k: &str) {
        if !a.iter().any(|x| x == k) && a.len() < cap {
            a.push(k.to_string())
        }
    }
    addu(&mut basic, 24, "SKILL_BasicPunchAttack");
    addu(&mut act, 96, "SKILL_EmergencyEscape");
    addu(&mut extra, 24, "SKILL_EmergencyEscape");
    for sl in 0..nsl {
        let it = b.eq[sl];
        if it < 0 {
            continue;
        }
        for (sk, ty) in &t.items[it as usize].skills {
            if *ty == 2 {
                addu(&mut basic, 24, sk)
            } else if *ty == 1 {
                addu(&mut pas, 96, sk);
                addu(&mut extra, 24, sk)
            } else {
                addu(&mut act, 96, sk);
                addu(&mut extra, 24, sk)
            }
        }
    }
    for sk in &b.learned {
        let key = &t.skill[*sk as usize];
        if t.sk_passive[*sk as usize] {
            addu(&mut pas, 96, key)
        } else {
            addu(&mut act, 96, key)
        }
    }
    let qlist = |v: &[String]| v.iter().map(|x| format!("\"{x}\"")).collect::<Vec<_>>().join(",");
    w!("{{\"Type\":8,\"Data\":{{\"$type\":\"Refactor.SkillComponentSaveData, Assembly-CSharp\",\"BasicSkills\":[{}", qlist(&basic));
    w!("],\"ActiveSkills\":[{}", qlist(&act));
    w!("],\"PassiveSkills\":[{}", qlist(&pas));
    w!("],\"EquipBypassList\":[],\"Cooldowns\":{{}},\"SkillDurations\":{{}},\"AutoUse\":{{");
    w!("{}", act.iter().map(|x| format!("\"{x}\":true")).collect::<Vec<_>>().join(","));
    w!("}}}}}},");
    w!("{{\"Type\":9,\"Data\":{{\"$type\":\"Refactor.CarryComponentSaveData, Assembly-CSharp\",\"CarriedGuid\":\"{ZERO}\",\"CarrierGuid\":\"{ZERO}\",\"IsDead\":false}}}},");
    w!("{{\"Type\":10,\"Data\":{{\"$type\":\"Refactor.UnitProfileComponentSaveData, Assembly-CSharp\",\"ProfileKey\":\"{}\",\"UnitNameTextKey\":", p.key);
    if !b.name.is_empty() {
        sq(&mut o, &b.name)
    } else {
        sq(&mut o, defname.as_bytes())
    }
    w!(",\"DefaultUnitNameTextKey\":\"{}\",\"UnitBioTextKey\":\"\",\"UnitPortraitID\":\"{}\",\"UnitVisualType\":{},\"HeadType\":{},\"GenderType\":{},\"BodyType\":{},\"SkinColorType\":{},\"HairType\":{},\"FacialDecoType\":{},\"EyesType\":{},\"HideHeadEquipment\":{}}}}},",
        defname, p.portrait, p.vis, p.head, p.gender, p.body, p.skin, p.hair, p.facial, p.eyes, if b.hide_head { "true" } else { "false" });
    w!("{{\"Type\":15,\"Data\":{{\"$type\":\"Refactor.TradeDataComponentSaveData, Assembly-CSharp\",\"GoldAmount\":0,\"HasRefreshTimer\":false,\"TradeRefreshType\":0,\"TradePoolRefreshKey\":\"TRADEPOOL_GuildMerchant\",\"TradeTickPassed\":0.0,\"TradeRefreshInterval\":0.0,\"DiscountRates\":[");
    w!("{}", vec!["0.0"; 36].join(","));
    w!("]}}}}],\"registry\":{{\"Guid\":\"{ug}\",\"Key\":\"UNIT_PlayerUnit\"}},\"position\":null,\"anim\":{{}},");
    w!("\"pusd\":{{\"SkillTrees\":{{\"SkillTrees\":{{\"Main\":[");
    let tl = |kind: i32| tr.iter().filter(|x| t.tree_kind[**x as usize] == kind).map(|x| format!("\"{}\"", t.tree[*x as usize])).collect::<Vec<_>>().join(",");
    w!("{}", tl(0));
    w!("],\"Sub\":[{}", tl(1));
    w!("],\"Other\":[]}},\"ExtraSkills\":[{}", qlist(&extra));
    w!("],\"UsedSkillPoints\":{{\"Main\":{},\"Sub\":{},\"Other\":0}},\"LearnedSkills\":{{", fmt_g6(used[0]), fmt_g6(used[1]));
    for (i, x) in tr.iter().enumerate() {
        w!("{}\"{}\":[", c(i), t.tree[*x as usize]);
        let ls: Vec<String> = b.learned.iter().filter(|sk| t.sk_tree[**sk as usize] == *x).map(|sk| format!("\"{}\"", t.skill[*sk as usize])).collect();
        w!("{}]", ls.join(","));
    }
    w!("}}}},\"SkillLearnNotices\":{{\"HasSnapshot\":true,\"ShowNotice\":false,\"MainSkillPoint\":{},\"SubSkillPoint\":{}}},", fmt_g6(budget[0] - used[0]), fmt_g6(budget[1] - used[1]));
    w!("\"QuickSlots\":{{\"QuickSlots\":[");
    let qs: Vec<String> = act.iter().filter(|x| *x != "SKILL_EmergencyEscape").take(5).map(|x| format!("\"{x}\"")).collect();
    w!("{}", qs.join(","));
    w!("],\"ItemQuickSlotKey\":null}},\"ConsumePolicies\":{{\"Policy\":{{}}}},\"TaskPriorities\":{{\"Priority\":{{\"PickUp\":2,\"Feeding\":2,\"Research\":2,\"Construct\":2,\"Demolition\":2,\"Store\":2,\"Crafting\":2,\"Cooking\":2,\"Farming\":2,\"Harvesting\":2,\"Gathering\":2,\"Logging\":2,\"Mining\":2}}}},\"WayPointQueues\":[],\"RecruitCosts\":0}},\"prayer\":null,\"exp_stamp\":null}},\"items\":{{");
    let mut ki = 0;
    for sl in 0..nsl {
        let it = b.eq[sl];
        if it < 0 {
            continue;
        }
        let item = &t.items[it as usize];
        w!("{}\"{}\":{{\"components\":[{{\"Type\":0,\"Data\":{{\"$type\":\"Refactor.StatusComponentSaveData, Assembly-CSharp\",\"SchemaVersion\":0,\"Stats\":{{", c(ki), ig[sl]);
        ki += 1;
        let mut tt = Stats::default();
        for f in FLAT_ITEM_STATS {
            tt.sget(f);
        }
        for (ix, v) in &b.tags[sl] {
            if let Some(x) = tt.sget(&item.tag_stat[*ix as usize]) {
                x.flat += *v as f64
            }
        }
        for x in &tt.s {
            w!("\"{}\":{},", x.st, fmt_g6(x.flat));
        }
        w!("\"Durability\":{}.0,\"MaxDurability\":{}.0}},\"StateStats\":null,\"ProgressionStats\":null,\"GeneratedStats\":null,\"PermanentStats\":null}}}},", item.dur, item.dur);
        w!("{{\"Type\":1,\"Data\":{{\"$type\":\"Refactor.EntityInfoComponentSaveData, Assembly-CSharp\",\"Faction\":0,\"IsDead\":false,\"Option\":0}}}},{{\"Type\":3,\"Data\":{{\"$type\":\"Refactor.AffecterComponentSaveData, Assembly-CSharp\",\"AffecterHolders\":[],\"InactiveMoodCauseHolders\":[]}}}}],\"registry\":{{\"Guid\":\"{}\",\"Key\":\"{}\"}},\"autofix\":true}}", ig[sl], t.item[it as usize]);
    }
    w!("}}}}");
    let cl = jparse(&o).map_err(|e| format!("internal: closure JSON: {e}"))?;
    // manifest
    let id = rng.id();
    let h = format!("{:016x}", jhash(&cl));
    let ps = fe_progress_sig(&cl);
    let hv = format!("{:016x}", wk_hash(b));
    let pr = fe_comp(cl.path("unit.components"), 10);
    let name_key = match pr.and_then(|p| p.get("UnitNameTextKey")) {
        Some(x) => x.clone(),
        None => J::Null,
    };
    let st = |s: &str| J::Str(s.as_bytes().to_vec());
    let man = J::Obj(vec![
        (b"export_id".to_vec(), st(&id)),
        (b"player_uid".to_vec(), st(player_uid)),
        (b"home_campaign_guid".to_vec(), st(ZERO)),
        (b"home_save_name".to_vec(), st("wiki link")),
        (b"home_save_version".to_vec(), st(&t.game_version)),
        (b"unit_guid".to_vec(), st(&ug)),
        (b"closure_hash".to_vec(), st(&h)),
        (b"progress_sig".to_vec(), st(&ps)),
        (b"name_key".to_vec(), name_key.clone()),
        (b"level".to_vec(), J::Num(b.lvl.to_string().into_bytes())),
        (b"exp".to_vec(), J::Num(b"0".to_vec())),
        (b"source".to_vec(), st("wiki")),
        (b"wiki_hash".to_vec(), st(&hv)),
    ]);
    let e = J::Obj(vec![(b"closure".to_vec(), cl), (b"manifest".to_vec(), man)]);
    let mut out = vec![];
    e.write(&mut out, false);
    let nk = match &name_key {
        J::Str(s) => String::from_utf8_lossy(s).into_owned(),
        _ => String::new(),
    };
    Ok(Entry {
        json: String::from_utf8_lossy(&out).into_owned(),
        notes: String::from_utf8_lossy(&notes).lines().map(str::to_string).collect(),
        unit_guid: ug,
        name_key: nk,
        closure_hash: h,
        progress_sig: ps,
        export_id: id,
    })
}

// ---------------------------------------------------------------- public entry points
const FP_MISMATCH: &str = "this link was made with other wiki data (another game version): make it again on the wiki";

/// the units of a link as the C decoder reads them; links it cannot read (`#b=` forms, `...#team=` with other
/// separators) go through `codec::decode_link` + `codec::encode_team` first
pub fn wire_from_link(link: &str) -> Result<(Vec<WireBuild>, bool), String> {
    let t = link.trim();
    let c_form = t.contains("team=") || !(t.contains('/') || t.contains('#') || t.contains(':'));
    if c_form {
        return decode_wire(link);
    }
    let d = codec::decode_link(t).map_err(|e| e.to_string())?;
    if d.units.is_empty() {
        return Err("no units in the link".into());
    }
    decode_wire(&codec::encode_team(&d.units))
}

/// cmd_wiki with `max` units (the mod passes its party share, at most 6): decode, refuse a fingerprint mismatch,
/// keep the first `max` units, one entry each; a refusal names its unit ("unit N: ...")
pub fn entries_from_link_max(link: &str, player_uid: &str, seed: Option<u64>, max: usize) -> Result<Vec<Entry>, String> {
    let mut rng = Rng::new(seed);
    let (units, fpm) = wire_from_link(link)?;
    if fpm {
        return Err(FP_MISMATCH.into());
    }
    let n = units.len().min(max.max(1));
    let mut out = vec![];
    for (i, u) in units.iter().take(n).enumerate() {
        out.push(pb_entry(u, player_uid, &mut rng).map_err(|e| format!("unit {}: {}", i + 1, e))?);
    }
    Ok(out)
}
/// cmd_wiki: at most 8 units (the link's own limit)
pub fn entries_from_link(link: &str, player_uid: &str, seed: Option<u64>) -> Result<Vec<Entry>, String> {
    entries_from_link_max(link, player_uid, seed, WK_MAX_UNITS)
}
/// the builder's team: encode_team, then exactly what the mod builds from that shared link
pub fn entries_from_builds(builds: &[Build], player_uid: &str, seed: Option<u64>) -> Result<Vec<Entry>, String> {
    if builds.is_empty() {
        return Err("no units".into());
    }
    entries_from_link(&codec::encode_team(builds), player_uid, seed)
}
/// pb_stored_majors for each unit of a link
pub fn stored_majors_from_link(link: &str) -> Result<Vec<[f64; 6]>, String> {
    Ok(wire_from_link(link)?.0.iter().map(stored_majors).collect())
}
