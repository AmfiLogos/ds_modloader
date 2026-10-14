// The wiki builder (site/builder/data.js + builder.js) loaded in a node vm, as the REFERENCE for the Rust engine's
// tests. Read only: builder.js runs its pure core; its UI part stops at the first DOM call (expected). savegen.js
// is never loaded.
//   node builder_ref.js gen N SEED      -> N random builds, one JSON line each
//   node builder_ref.js sheet           <- builds (JSON lines)  -> computeBuild + helpers per build
//   node builder_ref.js encode          <- builds               -> {v1, v2, b64, dec} (codes and the builder's own decode)
//   node builder_ref.js decode          <- codes (one per line)  -> decodeTeam result (v1 raw or v2 deflate-raw)
//   node builder_ref.js validate        <- builds               -> the build after validate()
// Input: stdin, or the file named by DSB_IN.
const fs = require('fs'), vm = require('vm'), path = require('path'), zlib = require('zlib');
const dir = process.env.DSB_DIR || path.join(process.env.HOME, 'dungeonsettlersdb', 'site', 'builder');
function load() {
  const noop = () => {}, el = new Proxy(function(){}, { get: (t, k) => k === Symbol.toPrimitive ? () => '' : el, apply: () => el, construct: () => el });
  const ctx = { console: { log: noop, warn: noop, error: noop, assert: noop }, setTimeout, clearTimeout, Promise,
    btoa: s => Buffer.from(s, 'binary').toString('base64'), atob: s => { if (/[^A-Za-z0-9+/=\s]/.test(s)) throw new Error('bad'); return Buffer.from(s, 'base64').toString('binary'); },
    unescape, escape, encodeURIComponent, decodeURIComponent, Math, JSON, Array, Object, String, Number, isNaN, parseInt, parseFloat, Date,
    document: el, location: { hash: '', search: '', pathname: '/builder/', href: 'https://dungeonsettlers.wiki/builder' }, navigator: { userAgent: 'node' },
    localStorage: { getItem: () => null, setItem: noop }, history: { replaceState: noop }, addEventListener: noop, requestAnimationFrame: noop };
  ctx.window = ctx; ctx.global = ctx; ctx.self = ctx;
  vm.createContext(ctx);
  vm.runInContext(fs.readFileSync(path.join(dir, 'data.js'), 'utf8'), ctx);
  try { vm.runInContext(fs.readFileSync(path.join(dir, 'builder.js'), 'utf8'), ctx, { filename: 'builder.js' }); } catch (e) { ctx.__err = String(e).slice(0, 200); }
  // validate() and its helpers live inside the UI closure: lift their source out (brace-matched) and run
  // them in the same context, with the globals they read (S, tourHoldBg)
  const src = fs.readFileSync(path.join(dir, 'builder.js'), 'utf8');
  const lift = name => {
    const at = src.indexOf('\nfunction ' + name + '(');
    if (at < 0) throw new Error('no function ' + name);
    // a top-level function of the closure runs until the next line that starts at column 0 with something
    // other than '}' (the file is formatted that way)
    const ls = src.slice(at + 1).split('\n'), keep = [ls[0]];
    for (let j = 1; j < ls.length && /^(\s|\}|$)/.test(ls[j]); j++) keep.push(ls[j]);
    return keep.join('\n');
  };
  const names = ['validate', 'raceOf', 'poolWeight', 'bgStarter', 'bgAvail', 'ammoOfPrimary', 'ammoFits', 'sanitizeEq', 'shelfOf',
    'treeOfSkillAny', 'shelfPut', 'shelfDrop', 'tagShelfOf', 'shelfTagsFor', 'unshelfTagsFor', 'setSlotItem', 'normTags', 'treeOfSkill', 'b64enc'];
  vm.runInContext('var S = null, tourHoldBg = false;\n' + names.map(lift).join('\n'), ctx, { filename: 'lifted.js' });
  return ctx;
}
const c = load(), D = c.D, X = c.dsbIdx();
const lines = () => fs.readFileSync(process.env.DSB_IN || 0, 'utf8').split('\n').filter(l => l.trim());
const out = o => process.stdout.write(JSON.stringify(o) + '\n');
const mode = process.argv[2];

function gen(N, seed) {
  let s = seed >>> 0 || 1;
  const rnd = n => { s = (Math.imul(s, 1103515245) + 12345) >>> 0; return (s >>> 8) % n; };
  const pick = a => a[rnd(a.length)];
  const chance = p => rnd(1000) < p * 1000;
  const races = X.RACE.concat([null, 'AFFECTER_Bogus']);
  const tiers = D.tiers.order.concat(['Bogus']);
  const skills = Object.keys(D.skills), items = Object.keys(D.items), inds = Object.keys(D.ind).concat(Object.keys((D.retired || {}).ind || {}));
  const inscs = D.insc.map(i => i.k), trees = Object.keys(D.trees), souls = Object.keys(D.souls || {}), acq = (D.acq || []).map(a => a.k);
  const stats = D.sv.map(e => e.k);
  const condKeys = []; (D.cond || []).forEach(g => g.opts.forEach(o => condKeys.push(o.k)));
  const res = [];
  for (let t = 0; t < N; t++) {
    const race = pick(races), R = D.races.find(r => r.k === race);
    const bgPool = R ? R.bg.map(p => p[0]).filter(k => D.bg[k]) : Object.keys(D.bg);
    const b = { race, lvl: chance(0.05) ? pick([0, 13, 2.5]) : 1 + rnd(12), offer: pick(['Least', 'Ordinary', 'Plentiful', undefined]),
      roll: pick(['min', 'avg', 'max', 'avg']), bg: chance(0.08) ? null : (chance(0.85) ? pick(bgPool) : pick(Object.keys(D.bg))),
      inds: [], tal: {}, insctrees: [], learned: [], insc: [], souls: [], acqInds: [], elix: {}, cond: [], eq: {}, tags: {}, manual: {},
      advanced: chance(0.2), addShield: chance(0.2), hideHead: chance(0.3) };
    if (b.offer === undefined) delete b.offer;
    D.majors.forEach(m => { if (!chance(0.05)) b.tal[m] = pick(tiers); });
    for (let k = rnd(4); k > 0; k--) { const v = pick(inds); if (b.inds.indexOf(v) < 0) b.inds.push(v); }
    if (chance(0.5)) for (let k = 1 + rnd(2); k > 0; k--) { const v = pick(trees); if (b.insctrees.indexOf(v) < 0) b.insctrees.push(v); }
    const myTrees = c.activeTrees(b);
    const treeSkills = []; myTrees.forEach(tr => D.trees[tr].rows.forEach(r => r.forEach(k => { if (k) treeSkills.push(k); })));
    for (let k = rnd(9); k > 0; k--) { const v = (treeSkills.length && chance(0.85)) ? pick(treeSkills) : pick(skills); if (b.learned.indexOf(v) < 0) b.learned.push(v); }
    // the passives that grant stats, often (Sharpshooter / Deep Water milestones, conditional grants)
    if (chance(0.4)) { const v = pick(Object.keys(D.passiveGrants)); if (b.learned.indexOf(v) < 0) b.learned.push(v); }
    for (let k = rnd(5); k > 0; k--) { const v = pick(inscs); if (b.insc.indexOf(v) < 0) b.insc.push(v); }
    if (chance(0.15) && b.insc.indexOf(D.consts.perfection.k) < 0) b.insc.push(D.consts.perfection.k);
    if (chance(0.3)) b.souls.push(pick(souls));
    if (chance(0.3)) for (let k = 1 + rnd(2); k > 0; k--) { const v = pick(acq); if (b.acqInds.indexOf(v) < 0) b.acqInds.push(v); }
    if (chance(0.3)) { b.elix[pick(D.majors)] = pick([1, 2, 5, 120, '3', 0, -1, 2.7]); }
    if (chance(0.3)) { b.cond.push(pick(condKeys)); if (chance(0.5)) b.cond.push(pick(condKeys)); }
    D.slots.forEach(slot => {
      if (!chance(0.6)) return;
      const ok = items.filter(k => D.items[k].slot === slot), k = (ok.length && chance(0.95)) ? pick(ok) : pick(items);
      b.eq[slot] = k; const it = D.items[k];
      if (it.tags && it.tags.length && chance(0.8)) {
        b.tags[slot] = [];
        for (let j = 1 + rnd(3); j > 0; j--) {
          const i = rnd(it.tags.length + (chance(0.1) ? 2 : 0)), tg = it.tags[i] || it.tags[0];
          const f = rnd(4);
          if (f === 0) b.tags[slot].push({ i, v: tg[1] + (tg[2] - tg[1]) * (rnd(256) / 255) });
          else if (f === 1) b.tags[slot].push(i);
          else if (f === 2) b.tags[slot].push({ i, d: 1 });
          else b.tags[slot].push({ i, v: Math.round((tg[1] + (tg[2] - tg[1]) * rnd(101) / 100) * 1000) / 1000 });
        }
        if (chance(0.1)) b.tags[slot].push(null);
      }
    });
    if (chance(0.3)) { b.manual[pick(D.majors)] = pick([20 + rnd(400) / 10, '33.5', '', 'abc', 0]); }
    if (chance(0.2)) { b.manual[pick(stats)] = pick([rnd(5000) / 10, '12', '']); }
    if (chance(0.15)) { b.majorBase = {}; D.majors.forEach(m => { if (!chance(0.1)) b.majorBase[m] = rnd(300) / 10; }); }
    if (chance(0.2)) { b.passiveStacks = {}; Object.keys(D.passiveGrants).forEach(k => { if (chance(0.3)) b.passiveStacks[k] = pick([0, 5, 10, 30, 120, 2600, 260]); }); }
    if (chance(0.15)) { b.stackLv = {}; Object.keys(D.passiveGrants).forEach(k => { if (chance(0.4)) b.stackLv[k] = pick([0, 1, 3, 6, 9, '2']); }); }
    if (chance(0.1)) b.savedInds = (chance(0.5) ? [] : [pick(inds), pick(inds)]);
    if (chance(0.1)) b.savedInsc = [pick(inscs)];
    if (chance(0.1)) b.extraAdj = [['CriticalChance', 10], ['StrengthPerModifier', -15], ['MaxHealthTotalPerModifier', 20], ['FooBar', 3]];
    if (chance(0.15)) b.wfill = pick([0, 37, 100, 150, '', '55']);
    if (chance(0.7)) b.name = pick(['Wiki ' + t, '  Spaced  ', 'Ünïcødé ' + t, 'x'.repeat(60), 'É'.repeat(30)]);
    const pool = (D.portraits || {})[race] || [];
    if (pool.length && chance(0.8)) b.portrait = pick(pool); else if (chance(0.1)) b.portrait = 'NotAPortrait';
    res.push(b);
  }
  return res;
}

function sheet(b) {
  const r = c.computeBuild(b);
  const hit = r.st._hit;
  const learnedCost = b.learned.map(k => { const t = c.treeOfSkillFor(k, b); return [k, t, t && D.skills[k] ? c.skillCost(D.skills[k], t, r.st) : null]; });
  const ss = c.unitSkillSets(b);
  return { st: r.st, majors: r.majors, majorsRaw: r.majorsRaw, majorFlat: r.majorFlat, bd: r.bd, leftover: r.leftover, talBump: r.talBump,
    manualKeys: r.manualKeys, passiveCrit: r.passiveCrit, pct: r.pct, hit,
    aux: { activeTrees: c.activeTrees(b), mainSP: c.mainSPBudget(b), subSP: c.subSPBudget(b), inscSlots: c.inscSlots(b), inscCap: c.inscCap(b),
      learnedCost, skillSets: ss, quick: c.quickFor(b), stack: c.stackPassives(b).map(sp => [sp.k, c.stackLvFor(b, sp.k), sp.learned]),
      est: c.estimates(r.st, false), estM: c.estimates(r.st, true) } };
}
function v2of(v1code) {
  const bytes = Buffer.from(v1code.replace(/-/g, '+').replace(/_/g, '/'), 'base64'); bytes[0] = 2;
  return zlib.deflateRawSync(bytes).toString('base64').replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}
function decodeCode(code) {
  const bytes = c.dsbB64ToBytes(code); if (!bytes) return null;
  let d = null; try { d = c._decodeBlob(bytes); } catch (e) { d = null; }
  if (d) return d;
  try { return c._decodeBlob(Array.from(zlib.inflateRawSync(Buffer.from(bytes)))); } catch (e) { return null; }
}

(async () => {
  if (mode === 'gen') gen(+process.argv[3] || 300, +process.argv[4] || 7).forEach(out);
  else if (mode === 'sheet') lines().forEach(l => out(sheet(JSON.parse(l))));
  else if (mode === 'encode') for (const l of lines()) {
    const b = JSON.parse(l), v1 = await c.encodeTeam([b]);
    out({ v1, v2: v2of(v1), b64: c.b64enc(b), dec: decodeCode(v1) });
  }
  else if (mode === 'team') { const bs = lines().map(l => JSON.parse(l)); const v1 = await c.encodeTeam(bs); out({ v1, v2: v2of(v1), dec: decodeCode(v1) }); }
  else if (mode === 'decode') lines().forEach(l => out(decodeCode(l.trim())));
  else if (mode === 'validate') lines().forEach(l => { c.S = JSON.parse(l); c.validate(); out(c.S); });
  else if (mode === 'probe') out({ err: c.__err, fp: X.fp, has: [typeof c.computeBuild, typeof c.validate, typeof c.encodeTeam] });
  else { process.stderr.write('mode?\n'); process.exit(2); }
  // builder.js arms UI timers on load: leave without waiting for them
  process.stdout.write('', () => process.exit(0));
})();
