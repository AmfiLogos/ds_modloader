// Emits data/unit_tables.json: what the PvP / Foreign Expedition export (src/export.rs, the Rust port of
// ~/ds_multiplayer/ext/pvp/src/pvp_build.c) needs, with the SAME derivations as ext/pvp/tools/gen_unit_tables.js
// (pvp_unit_tables.h) and ext/pvp/tools/gen_wiki_tables.js (pvp_wiki_tables.h), keyed by keys instead of C indices.
// Sources: the GAME's own tables (~/dungeonsettlersdb/data, game build named in index.json) and dump.cs of that build;
// the builder's data.js / builder.js (loaded in a node vm) only for the share-link index order, the portrait pools,
// the item tag ranges the link codec uses, the condition groups and the builder's passive model.
// Float values are written as STRINGS (the exact text the C header has) so Rust parses them correctly rounded.
//   node gen/gen_unit_tables.js > data/unit_tables.json        (from ~/ds_launcher/builder)
const fs = require('fs'), vm = require('vm'), path = require('path');
const HOME = process.env.HOME;
function load() {   // as ext/pvp/tools/wiki_codec_probe.js (savegen.js is never loaded)
  const dir = path.join(HOME, 'dungeonsettlersdb', 'site', 'builder');
  const noop = () => {}, el = new Proxy(function(){}, { get: (t, k) => k === Symbol.toPrimitive ? () => '' : el, apply: () => el, construct: () => el });
  const ctx = { console: { log: noop, warn: noop, error: noop }, setTimeout, clearTimeout, Promise,
                btoa: s => Buffer.from(s, 'binary').toString('base64'), atob: s => Buffer.from(s, 'base64').toString('binary'),
                unescape, escape, encodeURIComponent, decodeURIComponent, Math, JSON, Array, Object, String, Number, isNaN, parseInt, parseFloat, Date,
                document: el, location: { hash: '', search: '', pathname: '/builder/' }, navigator: { userAgent: 'node' }, localStorage: { getItem: () => null, setItem: noop },
                history: { replaceState: noop }, addEventListener: noop, requestAnimationFrame: noop };
  ctx.window = ctx; ctx.global = ctx; ctx.self = ctx;
  vm.createContext(ctx);
  vm.runInContext(fs.readFileSync(path.join(dir, 'data.js'), 'utf8'), ctx);
  try { vm.runInContext(fs.readFileSync(path.join(dir, 'builder.js'), 'utf8'), ctx, { filename: 'builder.js' }); } catch (e) { ctx.__err = String(e).slice(0, 200); }
  return ctx;
}
const c = load(), D = c.D, X = c.dsbIdx();
const GD = path.join(HOME, 'dungeonsettlersdb', 'data');
const tab = n => JSON.parse(fs.readFileSync(path.join(GD, n + '.json'), 'utf8'));
const byKey = a => { const o = {}; a.forEach(r => o[r.Key] = r); return o; };
const index = tab('index'), TRAIT = byKey(tab('TraitTable')), AFF = byKey(tab('AffecterTable')), ITEM = byKey(tab('ItemTable')),
      SKILL = byKey(tab('SkillTable')), GS = byKey(tab('GameSystemTable')), SV = byKey(tab('StatusValueTable')), PROF = tab('UnitProfileTable');
const TREES = tab('SkillTreeTable');
const MAJ = D.majors, TIER = D.tiers.order;
let diffs = 0;
const diff = (what, builder, game) => { diffs++; process.stderr.write(`DIFF ${what}: builder ${JSON.stringify(builder)} / game ${JSON.stringify(game)}\n`); };
const nul = v => v == null || v === 'NULL' || (Array.isArray(v) && (!v.length || v[0] === 'NULL'));
const list = v => nul(v) ? [] : v.filter(x => x !== 'NULL');
const num = v => String(+(+v).toFixed(6));          // the C header's float text
const out = { game_version: String(index.build), game_build: String(index.steam_buildid) };

/* ---- wire: the share-link index tables (pvp_wiki_tables.h) */
const tagsB = X.ITEM.map(k => (D.items[k].tags || []).map(t => [String(+t[1]), String(+t[2])]));
out.wire = { fp: X.fp, race: X.RACE, bg: X.BG, ind: X.IND, insc: X.INSC, tree: X.TREE, skill: X.SKILL, item: X.ITEM, stat: X.STAT, slot: X.SLOT,
  soul: X.SOUL, acq: X.ACQ, maj: MAJ, tier: TIER, item_tags: tagsB, portraits: X.RACE.map(r => (D.portraits || {})[r] || null),
  cond: (D.cond || []).map(g => g.opts.map(o => o.k)) };

/* ---- level-up and generation constants */
const gsf = k => { if (!GS[k]) throw new Error('missing ' + k); return GS[k].FloatValues; };
out.talent = TIER.map(t => MAJ.map(m => { const v = gsf(`GAMESYSTEM_LevelUp_Talent${m}${t}`); return [num(v[0]), num(v[1])]; }));
TIER.forEach(t => { const r = D.tiers.range[t], g = gsf(`GAMESYSTEM_LevelUp_TalentStrength${t}`); if (r[0] !== g[0] || r[1] !== g[1]) diff('talent range ' + t, r, g); });
const offer = gsf('GAMESYSTEM_LevelUp_StatIncreaseFactorByOffering');
out.offer = offer.map(num);
const sp = ['DefaultMainSkillPoint', 'DefaultSubSkillPoint', 'MainSkillPointIncrease', 'SubSkillPointIncrease'].map(k => gsf('GAMESYSTEM_LevelUp_' + k)[0]);
out.sp = sp;
out.max_level = gsf('GAMESYSTEM_LevelUp_MaxLevel')[0];
out.major_base = MAJ.map(m => num(SV['STATUS_' + m].PlayerDefaultValue));

/* ---- stat adjustments of affecters (majors, major PerModifiers, skill aptitudes) */
const STAT_OK = s => MAJ.indexOf(s) >= 0 || MAJ.some(m => s === m + 'PerModifier') || /SkillAptitude$/.test(s);
const adjOf = (arr, fv) => list(arr).map(x => { const i = x.lastIndexOf('_'), v = x.slice(i + 1), m = /^\{(\d+)\}$/.exec(v);
  return [x.slice(0, i), m ? +((fv || [])[+m[1]]) : +v]; });
const adjStr = a => a.filter(p => STAT_OK(p[0])).map(p => `${p[0]}=${num(p[1])}`).join(';');
const affAdj = k => { const r = TRAIT[k] || AFF[k]; return r ? adjOf(r.AdjustStats || r.AdjustStatsData, r.FloatValues) : null; };
const affs = new Set();
[X.RACE, X.BG, X.IND, X.INSC, X.SOUL, X.ACQ].forEach(a => a.forEach(k => affs.add(k)));
const rolls = r => MAJ.map(m => [num(r[m + 'PerModifierMin'] || 0), num(r[m + 'PerModifierMax'] || 0)]);
out.race_roll = {}; X.RACE.forEach(k => out.race_roll[k] = rolls(TRAIT[k]));
out.bg_roll = {}; X.BG.forEach(k => out.bg_roll[k] = rolls(TRAIT[k]));
const DUMP = fs.readFileSync(path.join(HOME, 'ds_skill_truth', 'dump_' + index.steam_buildid, 'dump.cs'), 'utf8');
const enumNames = e => new Set([...DUMP.matchAll(new RegExp(`public const ${e} (\\w+) = \\d+;`, 'g'))].map(m => m[1]));
const mainT = enumNames('MainSkillTreeType'), subT = enumNames('SubSkillTreeType');
if (!mainT.size || !subT.size) throw new Error('tree enums not found in dump.cs');
const known = t => X.TREE.indexOf(t) >= 0;   // C: tree index or -1 (dropped from the 4-slot rows)
out.bg_mains = {}; out.bg_subs = {};
X.BG.forEach(k => {
  out.bg_mains[k] = list(TRAIT[k].AvaliableMainSkillTreeTypes).slice(0, 4).filter(known);
  out.bg_subs[k] = list(TRAIT[k].AvaliableSubSkillTreeTypes).slice(0, 4).filter(known);
});
const treeInsc = t => [`AFFECTER_${t}Inscription`, `AFFECTER_${t.replace(/Magic$/, '')}Inscription`].find(k => TRAIT[k]) || null;
out.trees = {};
X.TREE.forEach(t => { const a = D.apt[t], k = treeInsc(t); if (k) affs.add(k);
  out.trees[t] = { kind: mainT.has(t) ? 0 : subT.has(t) ? 1 : -1, apt: a && SV['STATUS_' + a] ? a : null, insc: k }; });
/* ---- skills */
const treeOf = {};
TREES.forEach(r => { const t = r.Key.replace(/^SKILLTREE_/, '').replace(/_\d+$/, ''); [r.SkillKey_Column1, r.SkillKey_Column2, r.SkillKey_Column3].forEach(s => { if (!nul(s)) treeOf[s] = t; }); });
const bindOf = {}; Object.values(AFF).concat(Object.values(TRAIT)).forEach(r => { if (!nul(r.BindingValue) && SKILL[r.BindingValue]) bindOf[r.BindingValue] = bindOf[r.BindingValue] || r.Key; });
out.skills = {};
X.SKILL.forEach(k => { const r = SKILL[k];
  if (!r) { diff('skill missing from the game tables ' + k, k, null); out.skills[k] = { passive: 0, basic: 0, tree: null, sp: 0, bind: null }; return; }
  out.skills[k] = { passive: r.SkillTypeName === 'Passive' ? 1 : 0, basic: r.IsBasicAttackSkill ? 1 : 0, tree: treeOf[k] != null && known(treeOf[k]) ? treeOf[k] : null,
    sp: r.RequiredSkillPoint, bind: bindOf[k] || null }; });
/* ---- items */
const SLOTN = ['Primary', 'Secondary', 'Head', 'Body', 'Accessory1', 'Accessory2', 'Ammo'];
const tagsOf = r => list(r.RandomTags).map(x => { const i = x.lastIndexOf('_'), [lo, hi] = x.slice(i + 1).split('/').map(Number); return [x.slice(0, i), num(lo), num(hi)]; });
out.items = {};
X.ITEM.forEach(k => { const r = ITEM[k];
  if (!r) { diff('item missing from the game tables ' + k, k, null); out.items[k] = { slot: -1, skills: [], tags: [], tagamount: 0, dur: 0, adj: null, onequip: '' }; return; }
  list(r.Equipment_OnEquipAffecters).forEach(a => affs.add(a));
  out.items[k] = { slot: SLOTN.indexOf(r.EquipmentTypeName),
    skills: list(r.Equipment_ItemDefaultSkills).map(s => { const q = SKILL[s]; return [s, !q ? -1 : q.IsBasicAttackSkill ? 2 : q.SkillTypeName === 'Passive' ? 1 : 0]; }),
    tags: tagsOf(r), tagamount: r.RandomTagAmount || 0, dur: r.Durability || 0,
    adj: adjStr(adjOf(r.Equipment_AdditionalStatuses)), onequip: list(r.Equipment_OnEquipAffecters).join(';') }; });
/* ---- affecter adjustments (the C binary-search table: keys with a game row) */
out.aff_adj = {};
[...affs].filter(k => affAdj(k)).sort().forEach(k => out.aff_adj[k] = adjStr(affAdj(k)));
/* ---- profiles per race, in the builder's portrait pool order */
const VIS = ['HumanStandardMale', 'HumanStandardFemale', 'HumanFatUnisex', 'ElfStandardMale', 'ElfStandardFemale', 'ElfSlimUnisex', 'LizardMan', 'LycanMale', 'LycanFemale'];
const profBy = {}; PROF.forEach(r => { if (!profBy[r.Portrait] || r.AppearWeight > 0) profBy[r.Portrait] = r; });
out.profiles = {};
X.RACE.forEach(r => out.profiles[r] = ((D.portraits || {})[r] || []).map(p => { const g = profBy[p]; if (!g) { diff('portrait has no game profile ' + p, p, null); return null; }
  const fam = g.Key.replace(/^UNITVISUAL_/, '').replace(/_[^_]*$/, '');
  return { key: g.Key, portrait: p, vis: VIS.indexOf(fam), gender: g.GenderType, head: g.HeadType, body: g.BodyType, skin: g.SkinColor, facial: g.FacialDecoType,
    eyes: g.EyesType, hair: g.HairType }; }).filter(Boolean));
out.race_name = {}; X.RACE.forEach(r => out.race_name[r] = r.replace(/^AFFECTER_/, ''));
out.elixir = MAJ.map(m => { const s = SKILL[`SKILL_${m}Elixir`]; return num(s ? s.FloatValues[0] : 0); });
/* ---- the builder's passive model (data.js passiveGrants) */
out.pg = {};
X.SKILL.forEach(k => { const g = (D.passiveGrants || {})[k]; if (!g) return;
  out.pg[k] = { cond: g.cond ? 1 : 0, thr: (g.thr || []).slice(0, 8), aff: g.aff || null, per: g.per != null ? num(g.per) : '-1',
    adj: (g.adj || []).map(a => a[0] + '=' + num(a[1])).join(';') }; });
process.stdout.write(JSON.stringify(out) + '\n');
process.stderr.write(`${diffs} difference(s) between the builder data and the game tables\n`);
