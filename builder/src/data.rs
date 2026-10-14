//! The builder's game data: `data/dsb.json`, the DS wiki builder payload (site/builder/data.js, `window.DSB`)
//! written by `gen/gen_tables.js`. Only the fields the engine reads are typed here; the whole payload stays
//! available as JSON (`raw`) for the UI catalog (names, icons, descriptions).
use indexmap::IndexMap;
use serde::Deserialize;
use serde_json::Value;
use std::sync::OnceLock;

pub type Adj = Vec<(String, f64)>;

#[derive(Debug, Deserialize)]
pub struct SvRow {
    pub k: String,
    #[serde(default)]
    pub n: String,
    pub g: String,
    pub d: f64,
    pub c: Vec<f64>,
}

#[derive(Debug, Deserialize)]
pub struct Race {
    pub k: String,
    pub n: String,
    #[serde(default)]
    pub adj: Adj,
    #[serde(default)]
    pub rolls: Option<Vec<(f64, f64)>>,
    /// background pool: [key, weight]
    #[serde(default)]
    pub bg: Vec<(String, f64)>,
    /// individual trait pool: [key, weight]
    #[serde(default)]
    pub ind: Vec<(String, f64)>,
    #[serde(default, rename = "starterBg")]
    pub starter_bg: Option<Vec<String>>,
}

#[derive(Debug, Deserialize)]
pub struct Background {
    pub n: String,
    #[serde(default)]
    pub adj: Adj,
    #[serde(default)]
    pub rolls: Option<Vec<(f64, f64)>>,
    #[serde(default)]
    pub mains: Vec<String>,
    #[serde(default)]
    pub subs: Vec<(String, f64)>,
}

#[derive(Debug, Deserialize)]
pub struct Trait {
    #[serde(default)]
    pub k: Option<String>,
    pub n: String,
    #[serde(default)]
    pub adj: Adj,
    /// code-side gear effect [kind, value]
    #[serde(default)]
    pub gfx: Option<(String, f64)>,
    /// soul stone hit effect [kind, values...]
    #[serde(default)]
    pub fx: Option<Vec<Value>>,
    #[serde(default)]
    pub ic: Option<String>,
    /// acquired traits: race lock
    #[serde(default)]
    pub race: Option<String>,
}

#[derive(Debug, Deserialize)]
pub struct Insc {
    pub k: String,
    pub n: String,
    #[serde(default)]
    pub adj: Adj,
    #[serde(default)]
    pub fx: Option<(String, f64)>,
    #[serde(default)]
    pub ct: Option<String>,
    #[serde(default)]
    pub ic: Option<String>,
}

#[derive(Debug, Deserialize)]
pub struct ElixRow {
    pub m: String,
    pub k: String,
    pub n: String,
    pub per: f64,
}

#[derive(Debug, Deserialize)]
pub struct CondOpt {
    pub k: String,
    pub n: String,
    #[serde(default)]
    pub adj: Adj,
}

#[derive(Debug, Deserialize)]
pub struct CondGroup {
    pub g: String,
    pub n: String,
    #[serde(default)]
    pub opts: Vec<CondOpt>,
}

#[derive(Debug, Deserialize)]
pub struct Tiers {
    pub order: Vec<String>,
    pub range: IndexMap<String, (f64, f64)>,
}

#[derive(Debug, Deserialize)]
pub struct Perfection {
    pub k: String,
    pub bump: i64,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Consts {
    pub max_level: i64,
    pub build_cap: i64,
    #[serde(rename = "mainSP0")]
    pub main_sp0: f64,
    #[serde(rename = "subSP0")]
    pub sub_sp0: f64,
    #[serde(rename = "mainSPI")]
    pub main_spi: f64,
    #[serde(rename = "subSPI")]
    pub sub_spi: f64,
    pub insc_per_level: f64,
    #[serde(default)]
    pub growth_int: bool,
    #[serde(default)]
    pub sp_cost_min: Option<f64>,
    #[serde(default)]
    pub fin_mul: bool,
    pub major_base: IndexMap<String, f64>,
    #[serde(default)]
    pub banked_needs_skill: bool,
    pub perfection: Perfection,
    #[serde(default, rename = "subTreeInscSP")]
    pub sub_tree_insc_sp: f64,
    #[serde(default)]
    pub ind_max: f64,
}

#[derive(Debug, Deserialize)]
pub struct Tree {
    pub n: String,
    pub kind: String,
    pub rows: Vec<Vec<Option<String>>>,
}

#[derive(Debug, Deserialize)]
pub struct Skill {
    pub n: String,
    pub sp: f64,
    #[serde(default)]
    pub lv: f64,
    #[serde(default)]
    pub pre: Vec<String>,
    pub ty: String,
}

#[derive(Debug, Deserialize)]
pub struct Item {
    pub n: String,
    pub slot: String,
    #[serde(default)]
    pub sub: Option<String>,
    #[serde(default)]
    pub hp: f64,
    #[serde(default)]
    pub pa: f64,
    #[serde(default)]
    pub ma: f64,
    #[serde(default)]
    pub adds: Option<Adj>,
    #[serde(default)]
    pub aadj: Option<Adj>,
    /// rolled bonus pool: [stat, lo, hi]
    #[serde(default)]
    pub tags: Option<Vec<(String, f64, f64)>>,
    /// how many bonuses the item rolls
    #[serde(default)]
    pub tn: Option<f64>,
    /// two-handed
    #[serde(default)]
    pub th: Option<Value>,
    #[serde(default)]
    pub ammo: Option<String>,
    #[serde(default)]
    pub bsk: Option<String>,
    #[serde(default)]
    pub defskills: Option<Vec<String>>,
    #[serde(default)]
    pub gfx: Option<(String, f64)>,
    #[serde(default)]
    pub mag: Option<Value>,
    #[serde(default)]
    pub ic: Option<String>,
}

#[derive(Debug, Deserialize)]
pub struct PassiveGrant {
    #[serde(default)]
    pub n: Option<String>,
    #[serde(default)]
    pub adj: Adj,
    #[serde(default)]
    pub cond: Option<Value>,
    #[serde(default)]
    pub thr: Option<Vec<f64>>,
    #[serde(default)]
    pub per: Option<f64>,
    #[serde(default)]
    pub aff: Option<String>,
    #[serde(default)]
    pub an: Option<String>,
}

#[derive(Debug, Deserialize)]
pub struct HitSkill {
    pub eff: Vec<(String, f64)>,
    #[serde(default)]
    pub act: Option<Value>,
    #[serde(default)]
    pub max: Option<f64>,
}

#[derive(Debug, Deserialize, Default)]
pub struct Retired {
    #[serde(default)]
    pub races: IndexMap<String, String>,
    #[serde(default)]
    pub ind: IndexMap<String, String>,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Dsb {
    pub sv: Vec<SvRow>,
    #[serde(default)]
    pub statn: IndexMap<String, String>,
    pub majors: Vec<String>,
    pub races: Vec<Race>,
    pub bg: IndexMap<String, Background>,
    pub ind: IndexMap<String, Trait>,
    pub insc: Vec<Insc>,
    #[serde(default)]
    pub souls: IndexMap<String, Trait>,
    #[serde(default)]
    pub acq: Vec<Trait>,
    #[serde(default)]
    pub acq_order: Vec<String>,
    #[serde(default)]
    pub retired: Retired,
    #[serde(default)]
    pub elix: Vec<ElixRow>,
    #[serde(default)]
    pub elix_max: Option<f64>,
    #[serde(default)]
    pub cond: Vec<CondGroup>,
    pub tiers: Tiers,
    pub offer: IndexMap<String, f64>,
    pub consts: Consts,
    #[serde(default)]
    pub apt: IndexMap<String, String>,
    pub trees: IndexMap<String, Tree>,
    pub skills: IndexMap<String, Skill>,
    pub items: IndexMap<String, Item>,
    pub slots: Vec<String>,
    #[serde(default)]
    pub portraits: IndexMap<String, Vec<String>>,
    #[serde(default)]
    pub passive_grants: IndexMap<String, PassiveGrant>,
    #[serde(default)]
    pub hit_skills: IndexMap<String, HitSkill>,
    #[serde(default)]
    pub bar_only_skills: Vec<String>,
    #[serde(default)]
    pub playernames: IndexMap<String, Vec<String>>,
    #[serde(default)]
    pub savegen: Value,
    #[serde(default)]
    pub selftest: Vec<Value>,
}

impl Dsb {
    pub fn race(&self, k: &str) -> Option<&Race> {
        // computeBuild keeps the LAST row with that key
        self.races.iter().rev().find(|r| r.k == k)
    }
    pub fn acq_trait(&self, k: &str) -> Option<&Trait> {
        // ACQ map: a later row with the same key wins
        self.acq.iter().rev().find(|a| a.k.as_deref() == Some(k))
    }
    pub fn insc_row(&self, k: &str) -> Option<&Insc> {
        self.insc.iter().rev().find(|r| r.k == k)
    }
    pub fn elix_row(&self, m: &str) -> Option<&ElixRow> {
        self.elix.iter().find(|r| r.m == m)
    }
    pub fn tier_idx(&self, t: &str) -> usize {
        self.tiers.order.iter().position(|x| x == t).unwrap_or(1)
    }
    /// Steam build id the payload was generated from (savegen.tag)
    pub fn game_build(&self) -> &str {
        self.savegen.get("tag").and_then(Value::as_str).unwrap_or("")
    }
    /// the game's save version string of that build (savegen.siteVer)
    pub fn game_version(&self) -> &str {
        self.savegen.get("siteVer").and_then(Value::as_str).unwrap_or("")
    }
}

pub struct Data {
    pub d: Dsb,
    pub raw: Value,
}

static DSB_JSON: &str = include_str!("../data/dsb.json");
static DATA: OnceLock<Data> = OnceLock::new();

/// the embedded payload, parsed once
pub fn data() -> &'static Data {
    DATA.get_or_init(|| {
        let raw: Value = serde_json::from_str(DSB_JSON).expect("data/dsb.json");
        let d: Dsb = serde_json::from_value(raw.clone()).expect("data/dsb.json: payload shape");
        Data { d, raw }
    })
}
pub fn dsb() -> &'static Dsb {
    &data().d
}

/// JavaScript truthiness of a JSON value (`if (x)`)
pub fn truthy(v: Option<&Value>) -> bool {
    match v {
        None | Some(Value::Null) => false,
        Some(Value::Bool(b)) => *b,
        Some(Value::Number(n)) => n.as_f64().map(|x| x != 0.0 && !x.is_nan()).unwrap_or(false),
        Some(Value::String(s)) => !s.is_empty(),
        Some(_) => true,
    }
}

/// JavaScript unary `+x` (ToNumber) on a JSON value
pub fn js_num(v: &Value) -> f64 {
    match v {
        Value::Null => 0.0,
        Value::Bool(b) => {
            if *b {
                1.0
            } else {
                0.0
            }
        }
        Value::Number(n) => n.as_f64().unwrap_or(f64::NAN),
        Value::String(s) => js_str_num(s),
        Value::Array(a) => match a.len() {
            0 => 0.0,
            1 => js_num(&Value::String(match &a[0] {
                Value::String(s) => s.clone(),
                Value::Number(n) => n.to_string(),
                Value::Null => String::new(),
                Value::Bool(b) => b.to_string(),
                _ => return f64::NAN,
            })),
            _ => f64::NAN,
        },
        Value::Object(_) => f64::NAN,
    }
}

/// ToNumber of a string: trimmed, empty = 0, decimal / hex / Infinity, else NaN
pub fn js_str_num(s: &str) -> f64 {
    let t = s.trim_matches(|c: char| c.is_whitespace() || c == '\u{feff}');
    if t.is_empty() {
        return 0.0;
    }
    if let Some(h) = t.strip_prefix("0x").or_else(|| t.strip_prefix("0X")) {
        return u64::from_str_radix(h, 16).map(|v| v as f64).unwrap_or(f64::NAN);
    }
    match t {
        "Infinity" | "+Infinity" => return f64::INFINITY,
        "-Infinity" => return f64::NEG_INFINITY,
        _ => {}
    }
    if t.chars().any(|c| c.is_ascii_alphabetic() && c != 'e' && c != 'E') {
        return f64::NAN;
    }
    t.parse::<f64>().unwrap_or(f64::NAN)
}

/// JavaScript Math.round (halves toward +infinity)
pub fn js_round(x: f64) -> f64 {
    if !x.is_finite() {
        return x;
    }
    let f = x.floor();
    if x - f >= 0.5 {
        f + 1.0
    } else {
        f
    }
}

/// JavaScript ToInt32 (`x|0`)
pub fn to_i32(x: f64) -> i32 {
    if !x.is_finite() {
        return 0;
    }
    let t = x.trunc();
    let m = t.rem_euclid(4294967296.0);
    (m as u64 as u32) as i32
}

/// JavaScript ToUint32 (`x>>>0`)
pub fn to_u32(x: f64) -> u32 {
    to_i32(x) as u32
}
