//! The wiki builder's share links, both ways (port of builder.js dsbIdx / dsbEncBuild / dsbDecBuild /
//! encodeTeam / _decodeBlob / decodeTeam). Wire format: blob = [version, fingerprint hi, lo, unit count, units...],
//! LEB128 varints; v1 = base64url of the raw blob, v2 = base64url of the deflate-raw compressed blob.
//! Accepted link forms: `...?team=CODE`, `...#team=CODE`, `...#b=~CODE` (one unit), `...#b=<base64url JSON build>`
//! (the builder's full single-build link) and a bare CODE. The short links (`/api/shorten` ids) need the wiki
//! server to resolve them: open them once in a browser and copy the long link.
use crate::data::{dsb, js_round, to_i32, to_u32};
use crate::model::{Build, TagPick};
use flate2::read::DeflateDecoder;
use flate2::write::DeflateEncoder;
use flate2::Compression;
use indexmap::IndexMap;
use serde::Serialize;
use std::collections::HashMap;
use std::io::{Read, Write};
use std::sync::OnceLock;

pub const MAX_TEAM: usize = 8;
pub const WIKI_BUILDER_URL: &str = "https://dungeonsettlers.wiki/builder";

/// the link index tables (sorted keys; retired races / traits kept so old links keep their indices)
pub struct Idx {
    pub race: Vec<String>,
    pub bg: Vec<String>,
    pub ind: Vec<String>,
    pub insc: Vec<String>,
    pub tree: Vec<String>,
    pub skill: Vec<String>,
    pub item: Vec<String>,
    pub stat: Vec<String>,
    pub slot: Vec<String>,
    pub soul: Vec<String>,
    pub acq: Vec<String>,
    pub fp: u32,
    i_race: HashMap<String, usize>,
    i_bg: HashMap<String, usize>,
    i_ind: HashMap<String, usize>,
    i_insc: HashMap<String, usize>,
    i_tree: HashMap<String, usize>,
    i_skill: HashMap<String, usize>,
    i_item: HashMap<String, usize>,
    i_stat: HashMap<String, usize>,
    i_soul: HashMap<String, usize>,
    i_acq: HashMap<String, usize>,
}

fn inv(a: &[String]) -> HashMap<String, usize> {
    let mut m = HashMap::new();
    for (i, k) in a.iter().enumerate() {
        m.insert(k.clone(), i);
    }
    m
}
fn sorted(mut v: Vec<String>) -> Vec<String> {
    // JS default sort = UTF-16 code units; the keys are ASCII, so byte order is the same
    v.sort();
    v
}

pub fn idx() -> &'static Idx {
    static I: OnceLock<Idx> = OnceLock::new();
    I.get_or_init(|| {
        let d = dsb();
        let with_retired = |mut keys: Vec<String>, ret: &IndexMap<String, String>| {
            for k in ret.keys() {
                if !keys.contains(k) {
                    keys.push(k.clone())
                }
            }
            sorted(keys)
        };
        let race = with_retired(d.races.iter().map(|r| r.k.clone()).collect(), &d.retired.races);
        let bg = sorted(d.bg.keys().cloned().collect());
        let ind = with_retired(d.ind.keys().cloned().collect(), &d.retired.ind);
        let insc = sorted(d.insc.iter().map(|x| x.k.clone()).collect());
        let tree = sorted(d.trees.keys().cloned().collect());
        let skill = sorted(d.skills.keys().cloned().collect());
        let item = sorted(d.items.keys().cloned().collect());
        let stat = sorted(d.sv.iter().map(|e| e.k.clone()).collect());
        let slot = d.slots.clone();
        let sizes = [race.len(), bg.len(), ind.len(), insc.len(), tree.len(), skill.len(), item.len(), slot.len()];
        let mut fp: u32 = 0;
        for s in sizes {
            fp = (fp.wrapping_mul(131).wrapping_add(s as u32)) & 0xffff;
        }
        let soul = sorted(d.souls.keys().cloned().collect());
        let acq_keys: Vec<String> = d.acq.iter().filter_map(|a| a.k.clone()).collect();
        let mut acq: Vec<String> = if !d.acq_order.is_empty() {
            d.acq_order.iter().filter(|k| acq_keys.contains(k)).cloned().collect()
        } else {
            sorted(acq_keys.clone())
        };
        let mut uniq_sorted = acq_keys.clone();
        uniq_sorted.sort();
        uniq_sorted.dedup();
        for k in uniq_sorted {
            if !acq.contains(&k) {
                acq.push(k)
            }
        }
        Idx {
            i_race: inv(&race),
            i_bg: inv(&bg),
            i_ind: inv(&ind),
            i_insc: inv(&insc),
            i_tree: inv(&tree),
            i_skill: inv(&skill),
            i_item: inv(&item),
            i_stat: inv(&stat),
            i_soul: inv(&soul),
            i_acq: inv(&acq),
            race,
            bg,
            ind,
            insc,
            tree,
            skill,
            item,
            stat,
            slot,
            soul,
            acq,
            fp,
        }
    })
}

// ---- LEB128 writer / reader (builder.js DSBW / DSBR) ----
struct W {
    b: Vec<u8>,
}
impl W {
    fn u(&mut self, n: u32) {
        let mut n = n;
        while n > 0x7f {
            self.b.push(((n & 0x7f) | 0x80) as u8);
            n >>= 7;
        }
        self.b.push(n as u8);
    }
    fn uf(&mut self, n: f64) {
        self.u(to_u32(n))
    }
    fn byte(&mut self, n: u8) {
        self.b.push(n)
    }
    fn zz(&mut self, n: f64) {
        let n = to_i32(n);
        let v = if n < 0 { ((n.wrapping_neg()) << 1).wrapping_sub(1) } else { n << 1 };
        self.u(v as u32)
    }
    fn str(&mut self, s: &str) {
        let mut by = s.as_bytes().to_vec();
        if by.len() > 48 {
            let mut cut = 48;
            while cut > 0 && (by[cut] & 0xC0) == 0x80 {
                cut -= 1;
            }
            by.truncate(cut);
        }
        self.u(by.len() as u32);
        self.b.extend_from_slice(&by);
    }
}
struct R<'a> {
    b: &'a [u8],
    i: usize,
}
impl<'a> R<'a> {
    fn at(&self, i: usize) -> u8 {
        self.b.get(i).copied().unwrap_or(0)
    }
    fn u(&mut self) -> u32 {
        let mut n: i32 = 0;
        let mut sh: u32 = 0;
        loop {
            let x = self.at(self.i) as i32;
            self.i += 1;
            n |= (x & 0x7f).wrapping_shl(sh);
            sh += 7;
            if x & 0x80 == 0 {
                break;
            }
        }
        n as u32
    }
    fn byte(&mut self) -> u8 {
        let x = self.at(self.i);
        self.i += 1;
        x
    }
    fn zz(&mut self) -> i64 {
        let u = self.u() as i64;
        if u & 1 == 1 {
            -((u + 1) >> 1)
        } else {
            u >> 1
        }
    }
    fn str(&mut self) -> String {
        let len = self.u() as usize;
        let end = (self.i.saturating_add(len)).min(self.b.len());
        let a = if self.i < self.b.len() { self.b[self.i..end].to_vec() } else { vec![] };
        self.i = self.i.saturating_add(len);
        match String::from_utf8(a.clone()) {
            Ok(s) => s,
            Err(_) => a.iter().map(|c| *c as char).collect(),
        }
    }
    fn past_end(&self) -> bool {
        self.i > self.b.len()
    }
}

// ---- base64url ----
const B64: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
pub fn b64url_enc(a: &[u8]) -> String {
    let mut s = String::new();
    for ch in a.chunks(3) {
        let n = (ch[0] as u32) << 16 | (*ch.get(1).unwrap_or(&0) as u32) << 8 | *ch.get(2).unwrap_or(&0) as u32;
        let k = ch.len() + 1;
        for i in 0..k {
            s.push(B64[((n >> (18 - 6 * i)) & 63) as usize] as char);
        }
    }
    s
}
/// atob after '-'->'+', '_'->'/' and '=' padding; ASCII whitespace skipped (atob does), anything else invalid
pub fn b64url_dec(s: &str) -> Option<Vec<u8>> {
    let mut vals = vec![];
    let mut pad = false;
    for c in s.bytes() {
        let v = match c {
            b'A'..=b'Z' => c - b'A',
            b'a'..=b'z' => c - b'a' + 26,
            b'0'..=b'9' => c - b'0' + 52,
            b'-' | b'+' => 62,
            b'_' | b'/' => 63,
            b'=' => {
                pad = true;
                continue;
            }
            b' ' | b'\t' | b'\n' | b'\r' | 0x0c => continue,
            _ => return None,
        };
        if pad {
            return None;
        }
        vals.push(v as u32);
    }
    if vals.len() % 4 == 1 {
        return None;
    }
    let mut out = vec![];
    for ch in vals.chunks(4) {
        let mut n = 0u32;
        for (i, v) in ch.iter().enumerate() {
            n |= v << (18 - 6 * i);
        }
        let bytes = ch.len() * 6 / 8;
        for i in 0..bytes {
            out.push((n >> (16 - 8 * i)) as u8);
        }
    }
    Some(out)
}

fn tier_ix(name: Option<&String>) -> u32 {
    let o = &dsb().tiers.order;
    let mut i = name.and_then(|n| o.iter().position(|x| x == n));
    if i.is_none() {
        i = o.iter().position(|x| x == "Moderate");
    }
    i.unwrap_or(0) as u32
}

fn enc_build(w: &mut W, b: &Build) {
    let d = dsb();
    let x = idx();
    let maj = &d.majors;
    w.u(b.race.as_ref().and_then(|r| x.i_race.get(r)).copied().unwrap_or(0) as u32);
    w.u(b.bg.as_ref().and_then(|k| x.i_bg.get(k)).map(|i| *i as u32 + 1).unwrap_or(0));
    w.u(to_i32(b.lvl.unwrap_or(0.0)).min(255).max(1) as u32);
    let mut tp: u32 = 0;
    for m in maj {
        tp = tp * 5 + tier_ix(b.tal.get(m).filter(|s| !s.is_empty()));
    }
    w.u(tp);
    let offer = match b.offer.as_deref() {
        Some("Ordinary") => 1,
        Some("Plentiful") => 2,
        _ => 0,
    };
    let roll = match b.roll.as_deref() {
        Some("min") => 0,
        Some("max") => 2,
        _ => 1,
    };
    let name = b.name.as_deref().map(str::trim).filter(|s| !s.is_empty());
    let has_name = name.is_some() as u32;
    let port_idx = b
        .portrait
        .as_ref()
        .filter(|p| !p.is_empty())
        .and_then(|p| d.portraits.get(b.race.as_deref().unwrap_or("")).and_then(|pool| pool.iter().position(|x| x == p)));
    let has_port = port_idx.is_some() as u32;
    let man_keys: Vec<(&String, f64)> =
        b.manual.keys().filter_map(|k| Some((k, b.manual_val(k)?))).filter(|(k, _)| x.i_stat.contains_key(*k)).collect();
    let has_man = !man_keys.is_empty() as u32;
    let has_mb = b.major_base.as_ref().map(|mb| maj.iter().any(|m| matches!(mb.get(m), Some(Some(_))))).unwrap_or(false) as u32;
    let pstk: Vec<(&String, f64)> = b
        .passive_stacks
        .iter()
        .flatten()
        .filter_map(|(k, v)| {
            let v = (*v)?;
            (!v.is_nan() && v >= 0.0 && x.i_skill.contains_key(k)).then_some((k, v))
        })
        .collect();
    let has_pstk = !pstk.is_empty() as u32;
    let has_souls = !b.souls.is_empty() as u32;
    let has_si = b.saved_inds.as_ref().map(|v| !v.is_empty()).unwrap_or(false) as u32;
    let has_sc = b.saved_insc.as_ref().map(|v| !v.is_empty()).unwrap_or(false) as u32;
    let has_acq = !b.acq_inds.is_empty() as u32;
    let hide = b.hide_head as u32;
    let has_elix = maj.iter().any(|m| b.elix_uses(m) > 0) as u32;
    let has_cond = !b.cond_on().is_empty() as u32;
    let flags = offer
        | (roll << 2)
        | ((b.advanced as u32) << 4)
        | ((b.add_shield as u32) << 5)
        | (has_name << 6)
        | (has_port << 7)
        | (has_man << 8)
        | (has_mb << 9)
        | (has_pstk << 10)
        | (has_souls << 11)
        | (has_si << 12)
        | (has_sc << 13)
        | (has_acq << 14)
        | (hide << 15)
        | (has_elix << 16)
        | (has_cond << 17);
    w.u(flags);
    fn idx_list(w: &mut W, arr: &[String], inv: &HashMap<String, usize>) {
        let vals: Vec<u32> = arr.iter().filter_map(|k| inv.get(k).map(|i| *i as u32)).collect();
        w.u(vals.len() as u32);
        for v in vals {
            w.u(v)
        }
    }
    idx_list(w, &b.inds, &x.i_ind);
    idx_list(w, &b.insctrees, &x.i_tree);
    idx_list(w, &b.learned, &x.i_skill);
    idx_list(w, &b.insc, &x.i_insc);
    let mut eqmask: u32 = 0;
    let mut eq_items: Vec<(usize, u32, &String, &String)> = vec![];
    for (si, slot) in x.slot.iter().enumerate() {
        let Some(key) = b.eq.get(slot).filter(|k| !k.is_empty()) else { continue };
        if !d.items.contains_key(key) {
            continue;
        }
        let Some(ii) = x.i_item.get(key) else { continue };
        eqmask |= 1 << si;
        eq_items.push((si, *ii as u32, slot, key));
    }
    w.u(eqmask);
    for e in &eq_items {
        w.u(e.1)
    }
    let mut tagmask: u32 = 0;
    let mut tag_slots: Vec<Vec<(u32, f64, f64, f64)>> = vec![];
    for (si, _, slot, key) in &eq_items {
        let it = &d.items[*key];
        let mut real = vec![];
        for p in b.tags.get(*slot).into_iter().flatten() {
            let Some(p) = p else { continue };
            let Some(ix) = p.idx() else { continue };
            let Some(tags) = it.tags.as_ref() else { continue };
            if !(ix >= 0.0 && ix < tags.len() as f64) || ix.fract() != 0.0 {
                continue;
            }
            let tg = &tags[ix as usize];
            let v = match p.val() {
                Some(v) if p.is_obj() => v,
                _ => tg.1 + (tg.2 - tg.1) * 0.5,
            };
            real.push((ix as u32, tg.1, tg.2, v));
        }
        if !real.is_empty() {
            tagmask |= 1 << si;
            tag_slots.push(real);
        }
    }
    w.u(tagmask);
    for real in tag_slots {
        w.u(real.len() as u32);
        for (ix, lo, hi, v) in real {
            let frac = if hi > lo { js_round((v - lo) / (hi - lo) * 255.0) } else { 0.0 };
            let frac = frac.max(0.0).min(255.0);
            w.u(ix);
            w.byte(frac as u8);
        }
    }
    if let Some(n) = name {
        w.str(n)
    }
    if let Some(pi) = port_idx {
        w.u(pi as u32)
    }
    if has_man == 1 {
        w.u(man_keys.len() as u32);
        for (k, v) in man_keys {
            w.u(x.i_stat[k] as u32);
            w.zz(js_round(v * 100.0));
        }
    }
    if has_mb == 1 {
        let mb = b.major_base.as_ref().unwrap();
        for m in maj {
            let v = mb.get(m).copied().flatten().filter(|v| *v != 0.0 && !v.is_nan()).unwrap_or(0.0);
            w.zz(js_round(v * 100.0));
        }
    }
    if has_pstk == 1 {
        w.u(pstk.len() as u32);
        for (k, v) in pstk {
            w.u(x.i_skill[k] as u32);
            w.uf(v);
        }
    }
    if has_souls == 1 {
        idx_list(w, &b.souls, &x.i_soul)
    }
    if has_si == 1 {
        idx_list(w, b.saved_inds.as_ref().unwrap(), &x.i_ind)
    }
    if has_sc == 1 {
        idx_list(w, b.saved_insc.as_ref().unwrap(), &x.i_insc)
    }
    if has_acq == 1 {
        idx_list(w, &b.acq_inds, &x.i_acq)
    }
    if has_elix == 1 {
        w.u(maj.len() as u32);
        for m in maj {
            w.u(b.elix_uses(m) as u32)
        }
    }
    if has_cond == 1 {
        w.u(d.cond.len() as u32);
        for g in &d.cond {
            let mut ix = 0;
            for (i, o) in g.opts.iter().enumerate() {
                if ix == 0 && b.cond.contains(&o.k) {
                    ix = i + 1
                }
            }
            w.u(ix as u32);
        }
    }
}

fn dec_build(r: &mut R, gone: &mut Vec<String>) -> Build {
    let d = dsb();
    let x = idx();
    let maj = &d.majors;
    let o = &d.tiers.order;
    let mut b = Build::default();
    let ri = r.u() as usize;
    let mut race = x.race.get(ri).or(x.race.first()).cloned().unwrap_or_else(|| "AFFECTER_Human".into());
    if let Some(n) = d.retired.races.get(&race) {
        gone.push(n.clone());
        race = "AFFECTER_Human".into();
    }
    b.race = Some(race);
    let bgv = r.u() as usize;
    b.bg = if bgv > 0 { x.bg.get(bgv - 1).cloned() } else { None };
    b.lvl = Some(r.u() as f64);
    let mut tp = r.u();
    let mut tiers = vec![0u32; 6];
    for i in (0..6).rev() {
        tiers[i] = tp % 5;
        tp /= 5;
    }
    for (j, m) in maj.iter().enumerate() {
        b.tal.insert(m.clone(), o.get(tiers[j] as usize).cloned().unwrap_or_else(|| "Moderate".into()));
    }
    let flags = r.u();
    b.offer = Some(["Least", "Ordinary", "Plentiful"].get((flags & 3) as usize).copied().unwrap_or("Least").into());
    b.roll = Some(["min", "avg", "max"].get(((flags >> 2) & 3) as usize).copied().unwrap_or("avg").into());
    b.advanced = (flags >> 4) & 1 == 1;
    b.add_shield = (flags >> 5) & 1 == 1;
    b.hide_head = (flags >> 15) & 1 == 1;
    let bit = |n: u32| (flags >> n) & 1 == 1;
    let read_list = |r: &mut R, arr: &[String]| -> Vec<String> {
        let n = r.u();
        let mut out = vec![];
        for _ in 0..n {
            if r.past_end() {
                break; // a truncated blob is rejected by the caller anyway
            }
            let v = r.u() as usize;
            if let Some(k) = arr.get(v) {
                out.push(k.clone())
            }
        }
        out
    };
    let drop_gone = |list: Vec<String>, gone: &mut Vec<String>| -> Vec<String> {
        list.into_iter()
            .filter(|k| match d.retired.ind.get(k) {
                Some(n) => {
                    gone.push(n.clone());
                    false
                }
                None => true,
            })
            .collect()
    };
    let l = read_list(r, &x.ind);
    b.inds = drop_gone(l, gone);
    b.insctrees = read_list(r, &x.tree);
    b.learned = read_list(r, &x.skill);
    b.insc = read_list(r, &x.insc);
    let eqmask = r.u();
    let filled: Vec<&String> = x.slot.iter().enumerate().filter(|(si, _)| eqmask & (1 << si) != 0).map(|(_, s)| s).collect();
    for slot in filled {
        if let Some(k) = x.item.get(r.u() as usize) {
            b.eq.insert(slot.clone(), k.clone());
        }
    }
    let tagmask = r.u();
    for (si, slot) in x.slot.iter().enumerate() {
        if tagmask & (1 << si) == 0 {
            continue;
        }
        let n = r.u();
        let mut picks = vec![];
        for _ in 0..n {
            if r.past_end() {
                break;
            }
            let ix = r.u() as usize;
            let frac = r.byte() as f64;
            let it = b.eq.get(slot).and_then(|k| d.items.get(k));
            if let Some(tags) = it.and_then(|it| it.tags.as_ref()) {
                if ix < tags.len() {
                    let (lo, hi) = (tags[ix].1, tags[ix].2);
                    picks.push(Some(TagPick::Obj { i: Some(ix as f64), v: Some(lo + (frac / 255.0) * (hi - lo)), d: None }));
                }
            }
        }
        if !picks.is_empty() {
            b.tags.insert(slot.clone(), picks);
        }
    }
    if bit(6) {
        b.name = Some(r.str())
    }
    if bit(7) {
        let pi = r.u() as usize;
        if let Some(p) = d.portraits.get(b.race.as_deref().unwrap()).and_then(|pool| pool.get(pi)) {
            b.portrait = Some(p.clone())
        }
    }
    if bit(8) {
        let mn = r.u();
        for _ in 0..mn {
            if r.past_end() {
                break;
            }
            let si = r.u() as usize;
            let val = r.zz() as f64 / 100.0;
            if let Some(sk) = x.stat.get(si) {
                b.manual.insert(sk.clone(), serde_json::json!(val));
            }
        }
    }
    if bit(9) {
        let mut mb = IndexMap::new();
        for m in maj {
            mb.insert(m.clone(), Some(r.zz() as f64 / 100.0));
        }
        b.major_base = Some(mb);
    }
    if bit(10) {
        let mut ps = IndexMap::new();
        let np = r.u();
        for _ in 0..np {
            if r.past_end() {
                break;
            }
            let sk = x.skill.get(r.u() as usize).cloned();
            let st = r.u() as f64;
            if let Some(sk) = sk {
                ps.insert(sk, Some(st));
            }
        }
        b.passive_stacks = Some(ps);
    }
    if bit(11) {
        b.souls = read_list(r, &x.soul)
    }
    if bit(12) {
        let l = read_list(r, &x.ind);
        b.saved_inds = Some(drop_gone(l, gone))
    }
    if bit(13) {
        b.saved_insc = Some(read_list(r, &x.insc))
    }
    if bit(14) {
        b.acq_inds = read_list(r, &x.acq)
    }
    if bit(16) {
        let ne = r.u();
        for k in 0..ne as usize {
            if r.past_end() {
                break;
            }
            let nu = r.u();
            if nu > 0 {
                if let Some(m) = maj.get(k) {
                    b.elix.insert(m.clone(), serde_json::json!(nu));
                }
            }
        }
    }
    if bit(17) {
        let ng = r.u();
        for k in 0..ng as usize {
            if r.past_end() {
                break;
            }
            let ci = r.u() as usize;
            if let Some(g) = d.cond.get(k) {
                if ci > 0 {
                    if let Some(o) = g.opts.get(ci - 1) {
                        b.cond.push(o.k.clone())
                    }
                }
            }
        }
    }
    b
}

#[derive(Debug, Clone, Serialize)]
pub struct Decoded {
    pub units: Vec<Build>,
    /// the link was made with other wiki data (indices may point at other rows)
    pub warn: bool,
    /// names of retired races / traits the link referenced (dropped)
    pub gone: Vec<String>,
}

/// the raw team blob (header + units) for `version` 1 or 2
pub fn team_blob(builds: &[Build], version: u8) -> Vec<u8> {
    let x = idx();
    let mut w = W { b: vec![version, (x.fp >> 8) as u8, (x.fp & 0xff) as u8] };
    w.u(builds.len() as u32);
    for b in builds {
        enc_build(&mut w, b)
    }
    w.b
}

/// encodeTeam: the team code (v2 deflated when shorter, else v1 raw)
pub fn encode_team(builds: &[Build]) -> String {
    let raw2 = team_blob(builds, 2);
    let mut e = DeflateEncoder::new(Vec::new(), Compression::best());
    let comp = e.write_all(&raw2).ok().and_then(|_| e.finish().ok());
    match comp {
        // a deflate stream whose first byte reads as version 1 / 2 would be taken for a raw blob by every
        // decoder (they try raw first), so such a stream is never used
        Some(c) if c.len() < raw2.len() && c.first().map(|v| *v != 1 && *v != 2).unwrap_or(false) => b64url_enc(&c),
        _ => b64url_enc(&team_blob(builds, 1)),
    }
}
/// encodeTeam without compression (v1), as browsers without CompressionStream make it
pub fn encode_team_v1(builds: &[Build]) -> String {
    b64url_enc(&team_blob(builds, 1))
}

/// _decodeBlob
pub fn decode_blob(bytes: &[u8]) -> Option<Decoded> {
    if bytes.len() < 4 {
        return None;
    }
    let mut r = R { b: bytes, i: 0 };
    let ver = r.byte();
    if ver != 1 && ver != 2 {
        return None;
    }
    let fp = ((r.byte() as u32) << 8) | r.byte() as u32;
    let n = r.u();
    if n > 64 {
        return None;
    }
    let mut gone = vec![];
    let mut units = vec![];
    for _ in 0..n {
        units.push(dec_build(&mut r, &mut gone));
        if r.past_end() {
            return None;
        }
    }
    if r.past_end() {
        return None;
    }
    let mut g2: Vec<String> = vec![];
    for g in gone {
        if !g2.contains(&g) {
            g2.push(g)
        }
    }
    Some(Decoded { units, warn: fp != idx().fp, gone: g2 })
}

fn inflate(bytes: &[u8]) -> Option<Vec<u8>> {
    let mut out = vec![];
    let mut dec = DeflateDecoder::new(bytes).take(1 << 20);
    dec.read_to_end(&mut out).ok()?;
    Some(out)
}

/// decodeTeam on a bare code: raw (v1) first, then inflate (v2)
pub fn decode_code(code: &str) -> Option<Decoded> {
    let bytes = b64url_dec(code)?;
    if let Some(d) = decode_blob(&bytes) {
        return Some(d);
    }
    decode_blob(&inflate(&bytes)?)
}

fn pct_decode(s: &str) -> String {
    let b = s.as_bytes();
    let mut out = vec![];
    let mut i = 0;
    while i < b.len() {
        if b[i] == b'%' && i + 3 <= b.len() {
            if let Some(v) = std::str::from_utf8(&b[i + 1..i + 3]).ok().and_then(|h| u8::from_str_radix(h, 16).ok()) {
                out.push(v);
                i += 3;
                continue;
            }
        }
        out.push(b[i]);
        i += 1;
    }
    String::from_utf8(out).unwrap_or_else(|_| s.to_string())
}

#[derive(Debug, Clone, PartialEq)]
pub enum LinkError {
    Empty,
    ShortLink,
    NotALink,
    Corrupted,
}
impl std::fmt::Display for LinkError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(match self {
            LinkError::Empty => "No link.",
            LinkError::ShortLink => "This is a short link. Open it in a browser, then copy the long link from the address bar.",
            LinkError::NotALink => "This is not a builder link.",
            LinkError::Corrupted => "This link is damaged or cut short. Copy it again.",
        })
    }
}

/// any builder link or code -> its units
pub fn decode_link(text: &str) -> Result<Decoded, LinkError> {
    let t = text.trim();
    if t.is_empty() {
        return Err(LinkError::Empty);
    }
    // ?team= / #team= / &team=
    for pre in ["?team=", "#team=", "&team="] {
        if let Some(p) = t.find(pre) {
            let rest = &t[p + pre.len()..];
            let code: String = rest.chars().take_while(|c| *c != '&' && *c != '#' && !c.is_whitespace()).collect();
            return decode_code(&pct_decode(&code)).ok_or(LinkError::Corrupted);
        }
    }
    if let Some(p) = t.find("#b=") {
        let v = pct_decode(&t[p + 3..]);
        if let Some(code) = v.strip_prefix('~') {
            return decode_code(code).ok_or(LinkError::Corrupted);
        }
        // the full single-build link: base64url of the build JSON
        let bytes = b64url_dec(&v).ok_or(LinkError::Corrupted)?;
        let b: Build = serde_json::from_slice(&bytes).map_err(|_| LinkError::Corrupted)?;
        return Ok(Decoded { units: vec![b], warn: false, gone: vec![] });
    }
    if t.contains("://") || t.contains('/') {
        return Err(if t.contains("dungeonsettlers") { LinkError::ShortLink } else { LinkError::NotALink });
    }
    decode_code(t).ok_or(LinkError::NotALink)
}

/// the long team link the wiki opens (?team=CODE)
pub fn team_link(builds: &[Build]) -> String {
    format!("{WIKI_BUILDER_URL}?team={}", encode_team(builds))
}
/// the compact one-unit link (#b=~CODE), the builder's offline share form
pub fn solo_link(b: &Build) -> String {
    format!("{WIKI_BUILDER_URL}#b=~{}", encode_team(std::slice::from_ref(b)))
}

/// builder.js b64enc: the full single-build link payload (base64url of the build JSON)
pub fn b64_build(b: &Build) -> String {
    b64url_enc(serde_json::to_string(b).unwrap_or_default().as_bytes())
}

/// manual values as numbers (for callers that need f64)
pub fn manual_f64(b: &Build) -> IndexMap<String, f64> {
    b.manual.iter().filter_map(|(k, v)| Some((k.clone(), crate::model::manual_num(v)?))).collect::<IndexMap<_, _>>()
}
