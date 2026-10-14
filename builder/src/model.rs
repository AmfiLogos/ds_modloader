//! A unit build, in the wiki builder's own JSON shape (builder.js `defaultBuild()` / the `#b=` link / its
//! localStorage `dsb_builds_v1`). Field names are the builder's, so a build moves between the wiki and the
//! launcher unchanged. Fields the engine does not read (shelf, quick bar, ...) ride in `extra`.
use crate::data::{dsb, js_num, truthy};
use indexmap::IndexMap;
use serde::{Deserialize, Serialize};
use serde_json::Value;

/// one rolled bonus on an item: `{i: index into the item's tag pool, v: value}`; `d: 1` = a quick-set default
/// that follows the roll setting. A bare index (legacy) is also accepted.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(untagged)]
pub enum TagPick {
    Obj {
        #[serde(default, skip_serializing_if = "Option::is_none")]
        i: Option<f64>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        v: Option<f64>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        d: Option<Value>,
    },
    Idx(f64),
}

impl TagPick {
    pub fn idx(&self) -> Option<f64> {
        match self {
            TagPick::Obj { i, .. } => *i,
            TagPick::Idx(x) => Some(*x),
        }
    }
    /// the explicit value, if the pick carries one
    pub fn val(&self) -> Option<f64> {
        match self {
            TagPick::Obj { v, .. } => *v,
            TagPick::Idx(_) => None,
        }
    }
    pub fn is_obj(&self) -> bool {
        matches!(self, TagPick::Obj { .. })
    }
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Build {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub name: Option<String>,
    #[serde(default)]
    pub race: Option<String>,
    #[serde(default)]
    pub lvl: Option<f64>,
    #[serde(default)]
    pub offer: Option<String>,
    #[serde(default)]
    pub roll: Option<String>,
    #[serde(default)]
    pub bg: Option<String>,
    #[serde(default)]
    pub inds: Vec<String>,
    #[serde(default)]
    pub tal: IndexMap<String, String>,
    #[serde(default)]
    pub insctrees: Vec<String>,
    #[serde(default)]
    pub learned: Vec<String>,
    #[serde(default)]
    pub insc: Vec<String>,
    #[serde(default)]
    pub souls: Vec<String>,
    #[serde(default)]
    pub acq_inds: Vec<String>,
    /// stat elixir uses per major
    #[serde(default)]
    pub elix: IndexMap<String, Value>,
    /// hunger / exhaustion levels (affecter keys)
    #[serde(default)]
    pub cond: Vec<String>,
    #[serde(default)]
    pub eq: IndexMap<String, String>,
    #[serde(default)]
    pub tags: IndexMap<String, Vec<Option<TagPick>>>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub portrait: Option<String>,
    #[serde(default)]
    pub advanced: bool,
    /// "input my stats" overrides: stat key -> number (or a numeric string, as the builder's inputs store it)
    #[serde(default)]
    pub manual: IndexMap<String, Value>,
    #[serde(default)]
    pub add_shield: bool,
    #[serde(default)]
    pub hide_head: bool,
    /// save import: the unit's real accumulated attribute growth per major
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub major_base: Option<IndexMap<String, Option<f64>>>,
    /// banked counters of milestone passives (Sharpshooter kills, Deep Water summons)
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub passive_stacks: Option<IndexMap<String, Option<f64>>>,
    /// the milestone level picked per stacking passive
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub stack_lv: Option<IndexMap<String, Option<Value>>>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub saved_inds: Option<Vec<String>>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub saved_insc: Option<Vec<String>>,
    /// ad-hoc stat adjustments ("Environment")
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub extra_adj: Option<Vec<(String, f64)>>,
    /// share of Max Weight carried, percent (Heavy Plumb)
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub wfill: Option<Value>,
    /// everything else the builder stores (shelf, shelfTags, quick, icon, ...), kept as is
    #[serde(flatten)]
    pub extra: IndexMap<String, Value>,
}

impl Build {
    /// builder.js defaultBuild() (name and portrait aside: the caller picks them)
    pub fn default_build() -> Build {
        let d = dsb();
        let mut b = Build {
            race: Some("AFFECTER_Human".into()),
            lvl: Some(d.consts.build_cap as f64),
            offer: Some("Least".into()),
            roll: Some("avg".into()),
            portrait: d.portraits.get("AFFECTER_Human").and_then(|p| p.first().cloned()),
            ..Default::default()
        };
        for m in &d.majors {
            b.tal.insert(m.clone(), "Moderate".into());
        }
        b.extra.insert("shelf".into(), Value::Object(Default::default()));
        b.extra.insert("shelfTags".into(), Value::Object(Default::default()));
        b.extra.insert("quick".into(), Value::Null);
        b
    }
    /// `b.lvl || 1`
    pub fn level(&self) -> f64 {
        match self.lvl {
            Some(l) if l != 0.0 && !l.is_nan() => l,
            _ => 1.0,
        }
    }
    /// ({min:0, avg:0.5, max:1})[b.roll || 'avg']
    pub fn roll_q(&self) -> f64 {
        match self.roll.as_deref().filter(|s| !s.is_empty()).unwrap_or("avg") {
            "min" => 0.0,
            "max" => 1.0,
            "avg" => 0.5,
            _ => f64::NAN,
        }
    }
    /// the traits the stat sheet reads: b.savedInds || b.inds
    pub fn ind_list(&self) -> &Vec<String> {
        self.saved_inds.as_ref().unwrap_or(&self.inds)
    }
    pub fn is_imported(&self) -> bool {
        self.major_base.is_some()
    }
    /// elixUses: whole uses, 1..elixMax
    pub fn elix_uses(&self, m: &str) -> i64 {
        let raw = self.elix.get(m).map(js_num).unwrap_or(0.0);
        let n = if raw.is_nan() { 0.0 } else { raw.floor() };
        if n > 0.0 {
            let mx = dsb().elix_max.filter(|x| *x != 0.0).unwrap_or(99.0);
            n.min(mx) as i64
        } else {
            0
        }
    }
    /// manVal: a typed override, or None
    pub fn manual_val(&self, k: &str) -> Option<f64> {
        manual_num(self.manual.get(k)?)
    }
    /// the build's hunger / exhaustion levels in force (condOn): at most one per need, in data order
    pub fn cond_on(&self) -> Vec<&'static crate::data::CondOpt> {
        let mut out = vec![];
        if self.cond.is_empty() {
            return out;
        }
        for g in &dsb().cond {
            if let Some(o) = g.opts.iter().find(|o| self.cond.contains(&o.k)) {
                out.push(o);
            }
        }
        out
    }
    pub fn is_two_handed(&self) -> bool {
        let d = dsb();
        self.eq.get("Primary").and_then(|k| d.items.get(k)).map(|it| truthy(it.th.as_ref())).unwrap_or(false)
    }
}

/// `(mv==='' || mv==null || isNaN(mv)) ? null : +mv`
pub fn manual_num(v: &Value) -> Option<f64> {
    match v {
        Value::Null => None,
        Value::String(s) if s.is_empty() => None,
        _ => {
            let n = js_num(v);
            if n.is_nan() {
                None
            } else {
                Some(n)
            }
        }
    }
}
