// Test cases for the C unit writer (Custom Modes units/pvp_build.c; used by tests/export_vs_c.rs): random PLAYABLE builds (a race with a look, a background of that
// race, gear in its own slot, learned skills from the unit's trees within its points), encoded by the wiki's own encoder,
// with the builder's own numbers as the reference: stored majors (= 10 + Generated + Progression + Permanent, i.e. the
// builder's majorsRaw - majorFlat), skill points left, and the full sheet (for the in-game check).
// Usage: node make_build_cases.js N SEED > cases.jsonl
const c = require('./wiki_codec_probe.js').load(), D = c.D, X = c.dsbIdx();
let seed = +(process.argv[3] || 7); const rnd = n => { seed = (seed * 1103515245 + 12345) % 2147483648; return seed % n; };
const pick = a => a[rnd(a.length)];
const N = +(process.argv[2] || 40), TEAM = process.argv.indexOf('--team') > 0 ? +process.argv[process.argv.indexOf('--team') + 1] : 0;   // --team K: K units per link
const team = [];
const treeOf = {}; Object.keys(D.trees).forEach(t => (D.trees[t].rows || []).forEach(r => r.forEach(s => { if (s) treeOf[s] = t; })));
const races = X.RACE.filter(r => (D.portraits[r] || []).length);
// --edge: fixed builds that hit the rules where the builder and the game may differ (innate flat majors with the
// generation roll, negative roll sums, tree inscription points, Perfection, elixirs, a % bonus on a ring)
const EDGE = process.argv.indexOf('--edge') > 0;
function edge(i) {
  const T = (o) => Object.assign({ lvl: 3, tal: {}, offer: 'Ordinary', roll: 'min', advanced: false, addShield: false, hideHead: false, inds: [], insctrees: [], learned: [], insc: [], eq: {}, tags: {}, manual: {} }, o);
  const B = [
    T({ race: 'AFFECTER_Elf', bg: 'AFFECTER_Begger', inds: ['AFFECTER_SlowLearner'], learned: ['SKILL_SharpShooter'], name: 'Edge SlowLearner' }),   // Sharp Shooter: a milestone passive (Blitz tree of the Beggar),
    T({ race: 'AFFECTER_Human', bg: 'AFFECTER_Peddler', inds: ['AFFECTER_LightEater'], roll: 'max', name: 'Edge LightEater' }),
    T({ race: 'AFFECTER_Lycan', bg: (D.races.find(r => r.k === 'AFFECTER_Lycan').bg.find(p => p[1] > 0) || [null])[0], insctrees: [], insc: ['AFFECTER_PerfectionInscription'], lvl: 5, roll: 'avg', offer: 'Plentiful', name: 'Edge Perfection' }),
    T({ race: 'AFFECTER_LizardMan', bg: (D.races.find(r => r.k === 'AFFECTER_LizardMan').bg.find(p => p[1] > 0) || [null])[0], elix: { Strength: 3, Agility: 2 }, lvl: 4, roll: 'avg',
        eq: { Accessory2: 'ITEM_SilverRing' }, tags: { Accessory2: [{ i: 0, v: 10 }] }, name: 'Edge Elixir Ring' }),
  ];
  const b = B[i % B.length]; D.majors.forEach(m => b.tal[m] = b.tal[m] || 'Moderate');
  if (i % B.length === 2) { const own = c.activeTrees(b), t = X.TREE.find(x => own.indexOf(x) < 0 && D.trees[x] && D.trees[x].kind === 'main' && c.treeInscKey(x)); if (t) b.insctrees.push(t); }
  b.portrait = D.portraits[b.race][0];
  return b;
}
(async () => {
  for (let t = 0; t < N; t++) {
    if (EDGE) {
      const b = edge(t), trees = c.activeTrees(b);
      for (const sk of X.SKILL) { const tr = treeOf[sk]; if (!tr || trees.indexOf(tr) < 0 || b.learned.length >= 3) continue;
        const st = c.computeBuild(b).st, kind = D.trees[tr].kind, cost = c.skillCost(D.skills[sk], tr, st);
        const used = b.learned.filter(x => D.trees[treeOf[x]].kind === kind).reduce((a, x) => a + c.skillCost(D.skills[x], treeOf[x], st), 0);
        if (used + cost <= (kind === 'main' ? c.mainSPBudget(b) : c.subSPBudget(b)) && b.insctrees.indexOf(tr) >= 0 === (b.insctrees.length > 0 && b.learned.length === 0 ? true : b.insctrees.indexOf(tr) >= 0)) b.learned.push(sk); }
      const r = c.computeBuild(b), st = r.st;
      const usedK = k => b.learned.filter(x => D.trees[treeOf[x]].kind === k).reduce((a, x) => a + c.skillCost(D.skills[x], treeOf[x], st), 0);
      const one = { build: b, expect: { stored: D.majors.map(m => r.majorsRaw[m] - r.majorFlat[m]), spMain: c.mainSPBudget(b) - usedK('main'), spSub: c.subSPBudget(b) - usedK('sub'), majors: D.majors.map(m => r.majors[m]), st } };
      if (TEAM) { team.push(one); if (team.length === TEAM) process.stdout.write(JSON.stringify({ link: await c.encodeTeam(team.map(u => u.build)), units: team.splice(0) }) + '\n'); continue; }
      process.stdout.write(JSON.stringify(Object.assign({ link: await c.encodeTeam([b]) }, one)) + '\n'); continue;
    }
    const race = pick(races), R = D.races.find(x => x.k === race);
    const bgs = (R.bg || []).filter(p => p[1] > 0 && D.bg[p[0]]).map(p => p[0]);
    const b = { race, bg: bgs.length ? pick(bgs) : null, lvl: 1 + rnd(5), tal: {}, offer: pick(['Least', 'Ordinary', 'Plentiful']), roll: pick(['min', 'avg', 'max']),
      advanced: false, addShield: false, hideHead: !!rnd(2), inds: [], insctrees: [], learned: [], insc: [], eq: {}, tags: {}, manual: {} };
    D.majors.forEach(m => b.tal[m] = pick(D.tiers.order));
    for (let k = rnd(3); k > 0; k--) { const v = pick(X.IND.filter(i => !/Test/.test(i))); if (b.inds.indexOf(v) < 0) b.inds.push(v); }   // AFFECTER_TestSuperman: a developer trait, listed apart
    const own = c.activeTrees(b);
    if (rnd(2)) { const cand = X.TREE.filter(x => own.indexOf(x) < 0 && D.trees[x] && c.treeInscKey(x)); if (cand.length) b.insctrees.push(pick(cand)); }
    const plain = D.insc.map(i => i.k).filter(k => !c.dsbTreeInscSet()[k]);
    for (let k = rnd(3); k > 0; k--) { const v = pick(plain); if (b.insc.indexOf(v) < 0) b.insc.push(v); }
    X.SLOT.forEach(s => {
      if (!rnd(3)) return;
      const ok = X.ITEM.filter(k => D.items[k].slot === s); if (!ok.length) return;
      const k = pick(ok); b.eq[s] = k; const it = D.items[k];
      if (it.tags && it.tags.length) { const i = rnd(it.tags.length), tg = it.tags[i]; b.tags[s] = [{ i, v: tg[1] + (tg[2] - tg[1]) * (rnd(256) / 255) }]; }
    });
    if (rnd(3) === 0) { b.elix = {}; b.elix[pick(D.majors)] = 1 + rnd(3); }
    if (rnd(5) === 0) { b.majorBase = {}; D.majors.forEach(m => b.majorBase[m] = rnd(300) / 10); }
    if (rnd(6) === 0) b.manual[pick(D.majors)] = 20 + rnd(400) / 10;
    if (rnd(2) || TEAM) b.name = 'Wiki ' + t;
    const pool = D.portraits[race]; b.portrait = pick(pool);
    // learn skills while points last (builder's own cost and budgets)
    const trees = c.activeTrees(b);
    for (let tries = 0; tries < 12; tries++) {
      const st = c.computeBuild(b).st, sk = pick(X.SKILL), tr = treeOf[sk];
      if (!tr || trees.indexOf(tr) < 0 || b.learned.indexOf(sk) >= 0) continue;
      const kind = D.trees[tr].kind, cost = c.skillCost(D.skills[sk], tr, st);
      const used = b.learned.filter(x => D.trees[treeOf[x]].kind === kind).reduce((a, x) => a + c.skillCost(D.skills[x], treeOf[x], st), 0);
      const budget = kind === 'main' ? c.mainSPBudget(b) : c.subSPBudget(b);
      if (used + cost <= budget) b.learned.push(sk);
    }
    const r = c.computeBuild(b), st = r.st;
    const usedK = k => b.learned.filter(x => D.trees[treeOf[x]].kind === k).reduce((a, x) => a + c.skillCost(D.skills[x], treeOf[x], st), 0);
    const stored = D.majors.map(m => r.majorsRaw[m] - r.majorFlat[m]);
    const one = { build: b, expect: { stored, spMain: c.mainSPBudget(b) - usedK('main'), spSub: c.subSPBudget(b) - usedK('sub'), majors: D.majors.map(m => r.majors[m]), st } };
    if (TEAM) { team.push(one); if (team.length === TEAM) { process.stdout.write(JSON.stringify({ link: await c.encodeTeam(team.map(u => u.build)), units: team.splice(0) }) + '\n'); } continue; }
    process.stdout.write(JSON.stringify(Object.assign({ link: await c.encodeTeam([b]) }, one)) + '\n');
  }
})();
