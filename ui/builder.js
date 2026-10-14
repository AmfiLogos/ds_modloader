// Unit Builder and My Units screens. The rules and numbers live in Rust (ds-unit-builder, the wiki builder's
// engine): every change goes through builder_validate + builder_compute, this file only shows the result.
(function () {
"use strict";
const inv = (cmd, args) => window.__TAURI__.core.invoke(cmd, args || {});
const byId = (id) => document.getElementById(id);

function h(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined && text !== null) e.textContent = text;
  return e;
}
function btn(cls, text, onclick) {
  const b = h("button", cls, text);
  b.type = "button";
  if (onclick) b.onclick = onclick;
  return b;
}
function icon(path, cls) {
  const i = h("img", cls || "ico");
  i.alt = "";
  i.loading = "lazy";
  if (path) i.src = "bicons/" + String(path).replace(/^icons\//, "");
  return i;
}
function portraitPath(p) { return p ? "icons/portraits/" + p + ".png" : null; }

// ---------- number display (the wiki builder's fmtN / isPct / adjChip, display only) ----------
function fmtN(v, dec) {
  if (v == null || isNaN(v)) return "-";
  if (dec == null) dec = Math.abs(v) >= 100 ? 0 : Math.abs(v) >= 10 ? 1 : 2;
  return v.toFixed(dec).replace(/\.0+$/, "").replace(/(\.\d*?)0+$/, "$1");
}
const isMult = (s) => /PerModifier$|FinalMultiflier$/.test(s);
const isPct = (s) => !!D.pct[s] || /PerModifier$/.test(s);
const statLabel = (s) => { const b = s.replace(/PerModifier$/, "").replace(/FinalMultiflier$/, ""); return D.statn[b] || b; };
function adjText1(s, v) {
  if (isMult(s)) return (v >= 0 ? "+" : "") + fmtN(v) + "% " + statLabel(s);
  return (v >= 0 ? "+" : "") + fmtN(v) + (isPct(s) ? "%" : "") + " " + statLabel(s);
}
function adjList(adj) {
  const box = h("div", "ub-adj");
  (adj || []).forEach(([s, v]) => box.appendChild(h("span", v >= 0 ? "pos" : "neg", adjText1(s, v))));
  return box;
}
const resistPct = (r) => (100 * r) / (100 + r);

// ---------- state ----------
let D = null;           // the builder payload (data/dsb.json)
let CAT = null;         // builder_catalog
let S = null;           // the build being edited (builder JSON shape)
let L = null;           // builder_compute of S
let unitId = null;      // the "my units" entry S was opened from / saved to
let savedText = null;   // JSON of S when last saved (unsaved changes = differs)
let step = 1;
let seq = 0;
let focusSkill = null;
let gearSlot = "Primary";
let inscCat = "all";
let inscSearch = "";
let gearSearch = "";
let addingTree = false;
let units = [], teams = [];
const picked = new Set();

const STEPS = [
  [1, "Race", "Pick the race of your unit."],
  [2, "Background", "The background gives the starting skill trees and stat changes. Only backgrounds this race can have are listed."],
  [3, "Traits", "Pick the traits of your unit."],
  [4, "Level and talents", "Set the level, how much each level-up adds, the talent of each attribute and the elixirs drunk."],
  [5, "Skill trees", "Click a skill to learn it. Click a learned skill, or right-click it, to remove it."],
  [6, "Inscriptions", "You gain one inscription per level after level 1. Extra skill trees use them too."],
  [7, "Soul stones and acquired traits", "A unit holds one soul stone. Acquired traits come from items and events in play."],
  [8, "Gear", "Pick a slot, then an item. Set the bonus rolls of the item below it."],
  [9, "Name and portrait", "Name your unit and pick its face."],
];

function msg(text, warn) {
  const m = byId("ub-msg");
  m.textContent = text || "";
  m.classList.toggle("warn-text", !!warn);
}
function muMsg(text, warn) {
  const m = byId("mu-msg");
  m.textContent = text || "";
  m.classList.toggle("warn-text", !!warn);
}
const errText = (e) => (typeof e === "string" ? e : (e && e.message) || JSON.stringify(e));

const race = () => D.races.find((r) => r.k === S.race) || null;
const poolW = (pairs, k) => { let w = 0; (pairs || []).forEach((p) => { if (p[0] === k) w = p[1]; }); return w; };
const bgStarter = (r, k) => !!(r && r.starterBg && r.starterBg.indexOf(k) >= 0);
const bgAvail = (r, k) => poolW(r && r.bg, k) > 0 || bgStarter(r, k);
const indAvail = (r, k) => poolW(r && r.ind, k) > 0;
const acqAvail = (r, a) => !a.race || !!(r && a.race === r.k);
const indCap = () => D.consts.indMax || 4;
const treeName = (t) => (D.trees[t] ? D.trees[t].n : t);

// one change: validate then compute; the latest call wins
async function apply(cmd, args) {
  const my = ++seq;
  try {
    const b = await inv(cmd || "builder_validate", args || { build: S });
    const live = await inv("builder_compute", { build: b });
    if (my !== seq) return;
    S = b; L = live;
    render();
  } catch (e) { msg(errText(e), true); }
}
// edits a copy: S and L only change together, once the engine answered
function change(fn) { const b = JSON.parse(JSON.stringify(S)); fn(b); return apply(null, { build: b }); }

// ---------- plan ----------
function stepSummary(n) {
  const r = race();
  switch (n) {
    case 1: return [r ? r.n : "Not set", !!r];
    case 2: return [S.bg && D.bg[S.bg] ? D.bg[S.bg].n : "Not set", !!S.bg];
    case 3: return [S.inds.length + " of " + indCap(), S.inds.length > 0];
    case 4: {
      const set = D.majors.filter((m) => (S.tal[m] || "Moderate") !== "Moderate").length;
      return ["LV. " + S.lvl + " · " + set + (set === 1 ? " talent set" : " talents set"), true];
    }
    case 5: {
      const n = S.learned.length;
      return [L.activeTrees.map(treeName).join(" + ") + (n ? " · " + n + " skills" : ""), n > 0];
    }
    case 6: {
      const n = S.insctrees.length + S.insc.length;
      return [n ? n + " of " + L.inscSlots + " used" : "Not set", n > 0];
    }
    case 7: {
      const n = S.souls.length + S.acqInds.length;
      return [n ? (S.souls.length ? D.souls[S.souls[0]].n : "") + (S.acqInds.length ? (S.souls.length ? " · " : "") + S.acqInds.length + " traits" : "") : "Not set", n > 0];
    }
    case 8: {
      const n = Object.keys(S.eq).length;
      return [n + " of " + D.slots.length + " slots", n > 0];
    }
    case 9: return [S.name || "Not set", !!S.name];
  }
  return ["", false];
}
function renderPlan() {
  const ol = byId("ub-steps");
  ol.innerHTML = "";
  STEPS.forEach(([n, title]) => {
    const [sum, done] = stepSummary(n);
    const li = h("li");
    const b = btn("ub-stepbtn" + (n === step ? " on" : "") + (done ? " done" : ""), null, () => { step = n; focusSkill = null; addingTree = false; render(); });
    b.setAttribute("aria-current", n === step ? "step" : "false");
    b.appendChild(h("span", "ub-num", String(n)));
    const t = h("span", "ub-steptext");
    t.appendChild(h("span", "ub-stepname", title));
    t.appendChild(h("span", "ub-stepsum", sum));
    b.appendChild(t);
    if (done) b.appendChild(h("span", "ub-check", "✓"));
    li.appendChild(b);
    ol.appendChild(li);
  });
}

// ---------- unit card ----------
function renderCard() {
  const c = byId("ub-card");
  c.innerHTML = "";
  c.appendChild(icon(portraitPath(S.portrait), "ub-portrait"));
  const t = h("div", "ub-card-text");
  t.appendChild(h("div", "ub-name", S.name || "New unit"));
  const r = race();
  t.appendChild(h("div", "ub-sub", [r && r.n, S.bg && D.bg[S.bg] && D.bg[S.bg].n, "LV. " + S.lvl].filter(Boolean).join(" · ")));
  const dirty = JSON.stringify(S) !== savedText;
  t.appendChild(h("div", "ub-saved" + (dirty ? " dirty" : ""), unitId ? (dirty ? "Changes not saved" : "Saved in My units") : "Not in My units yet"));
  c.appendChild(t);
  const q = h("div", "ub-quick");
  (L.quick || []).forEach((k) => {
    const sk = D.skills[k];
    if (!sk) return;
    const s = h("span", "ub-qslot");
    s.appendChild(icon(sk.ic));
    s.title = sk.n;
    q.appendChild(s);
  });
  c.appendChild(q);
}

// ---------- steps ----------
function cardBtn(on, disabled, onclick) {
  const b = btn("ub-opt" + (on ? " on" : "") + (disabled ? " off" : ""), null, disabled ? null : onclick);
  b.setAttribute("aria-pressed", on ? "true" : "false");
  if (disabled) b.setAttribute("aria-disabled", "true");
  return b;
}
function optHead(b, ic, name, tag) {
  const hd = h("div", "ub-opt-h");
  if (ic) hd.appendChild(icon(ic));
  hd.appendChild(h("span", "ub-opt-n", name));
  if (tag) hd.appendChild(h("span", "tag", tag));
  b.appendChild(hd);
}

function stepRace(box) {
  const g = h("div", "ub-grid");
  D.races.forEach((r) => {
    const b = cardBtn(S.race === r.k, false, () => change((s) => {
      if (s.race === r.k) return;
      s.race = r.k;
      s.portrait = (D.portraits[r.k] || [])[0] || null;
      if (s.bg && !bgAvail(r, s.bg)) s.bg = null;
      s.inds = s.inds.filter((k) => indAvail(r, k));
      s.acqInds = s.acqInds.filter((k) => { const a = D.acq.find((x) => x.k === k); return a && acqAvail(r, a); });
    }));
    optHead(b, r.ic, r.n);
    b.appendChild(adjList(r.adj));
    g.appendChild(b);
  });
  box.appendChild(g);
}

function stepBackground(box) {
  const r = race();
  const keys = [];
  (r.bg || []).forEach((p) => { if (D.bg[p[0]] && bgAvail(r, p[0]) && keys.indexOf(p[0]) < 0) keys.push(p[0]); });
  (r.starterBg || []).forEach((k) => { if (D.bg[k] && keys.indexOf(k) < 0) keys.push(k); });
  const g = h("div", "ub-grid");
  keys.forEach((k) => {
    const bg = D.bg[k];
    const b = cardBtn(S.bg === k, false, () => change((s) => { s.bg = k; }));
    optHead(b, bg.ic, bg.n, bgStarter(r, k) ? "Starter" : null);
    const trees = bg.mains.map(treeName).concat(bg.subs.map((p) => treeName(p[0])));
    b.appendChild(h("div", "ub-opt-meta", "Trees: " + trees.join(", ")));
    b.appendChild(adjList(bg.adj));
    if (bg.d) b.appendChild(h("div", "ub-opt-d", bg.d));
    g.appendChild(b);
  });
  box.appendChild(g);
}

function stepTraits(box) {
  const r = race();
  box.appendChild(h("p", "meta", S.inds.length + " of " + indCap() + " picked. Click a picked trait to remove it."));
  const g = h("div", "ub-grid dense");
  (r.ind || []).forEach((p) => {
    const k = p[0], t = D.ind[k];
    if (!t || !indAvail(r, k)) return;
    const on = S.inds.indexOf(k) >= 0;
    const b = cardBtn(on, !on && S.inds.length >= indCap(), () => change((s) => {
      if (on) s.inds = s.inds.filter((x) => x !== k); else s.inds.push(k);
    }));
    optHead(b, t.ic, t.n);
    b.appendChild(adjList(t.adj));
    if (t.d) b.appendChild(h("div", "ub-opt-d", t.d));
    g.appendChild(b);
  });
  box.appendChild(g);
}

function chipRow(label, opts, cur, onpick) {
  const row = h("div", "ub-chiprow");
  row.appendChild(h("span", "ub-label", label));
  opts.forEach(([v, text]) => {
    const c = btn("chip", text, () => onpick(v));
    c.setAttribute("aria-pressed", cur === v ? "true" : "false");
    row.appendChild(c);
  });
  return row;
}

function stepLevel(box) {
  const lv = h("div", "ub-level");
  lv.appendChild(h("span", "ub-label", "Level"));
  const rng = h("input");
  rng.type = "range"; rng.min = 1; rng.max = D.consts.maxLevel; rng.value = S.lvl; rng.id = "ub-lvl";
  rng.setAttribute("aria-label", "Level");
  const out = h("b", "ub-lvlval", String(S.lvl));
  rng.oninput = () => { out.textContent = rng.value; };
  rng.onchange = () => change((s) => { s.lvl = +rng.value; });
  lv.appendChild(rng); lv.appendChild(out);
  lv.appendChild(h("span", "meta", "Max " + D.consts.maxLevel));
  box.appendChild(lv);
  box.appendChild(chipRow("Offering", Object.keys(D.offer).map((o) => [o, (D.offerName[o] || o) + " ×" + D.offer[o]]), S.offer,
    (v) => change((s) => { s.offer = v; })));
  box.appendChild(chipRow("Gain per level", [["min", "Lowest"], ["avg", "Average"], ["max", "Highest"]], S.roll,
    (v) => change((s) => { s.roll = v; })));

  box.appendChild(h("h3", "ub-h3", "Talents"));
  const tg = h("div", "ub-talgrid");
  D.majors.forEach((m) => {
    const name = h("div", "ub-taln");
    if (D.statIcons[m]) name.appendChild(icon(D.statIcons[m]));
    name.appendChild(h("span", null, statLabel(m)));
    tg.appendChild(name);
    const sq = h("div", "ub-talsq");
    D.tiers.order.forEach((t) => {
      const on = (S.tal[m] || "Moderate") === t;
      const r = D.tiers.range[t];
      const b = btn("ub-tsq" + (on ? " on" : "") + (D.tiers.save[t] === 0 ? " base" : ""), D.tiers.labels[t] || t,
        () => change((s) => { s.tal[m] = t; }));
      b.setAttribute("aria-pressed", on ? "true" : "false");
      b.title = (D.tiers.labels[t] || t) + ": +" + r[0] + " to " + r[1] + " per level";
      sq.appendChild(b);
    });
    tg.appendChild(sq);
  });
  box.appendChild(tg);

  box.appendChild(h("h3", "ub-h3", "Elixirs"));
  const eg = h("div", "ub-elix");
  D.elix.forEach((e) => {
    const row = h("label", "ub-elixrow");
    row.appendChild(icon(e.ic));
    row.appendChild(h("span", null, e.n));
    const n = h("input");
    n.type = "number"; n.min = 0; n.max = D.elixMax; n.step = 1;
    n.value = Number(S.elix[e.m] || 0);
    n.setAttribute("aria-label", e.n + " drunk");
    n.onchange = () => change((s) => {
      const v = Math.max(0, Math.min(D.elixMax, Math.floor(+n.value || 0)));
      if (v) s.elix[e.m] = v; else delete s.elix[e.m];
    });
    row.appendChild(n);
    row.appendChild(h("span", "meta", "+" + e.per + " " + statLabel(e.m) + " each"));
    eg.appendChild(row);
  });
  box.appendChild(eg);
}

function skillCostOf(k, t) {
  const c = (L.skillCosts || []).find((x) => x.skill === k && x.tree === t);
  return c && c.cost != null ? c.cost : (D.skills[k] ? D.skills[k].sp : 0);
}
function stepTrees(box) {
  const mainLeft = L.mainSp - L.mainSpUsed, subLeft = L.subSp - L.subSpUsed;
  const pools = h("div", "ub-pools");
  pools.appendChild(h("span", null, "Weapon skill points left: "));
  pools.appendChild(h("b", mainLeft < 0 ? "neg" : null, fmtN(mainLeft) + " of " + fmtN(L.mainSp)));
  pools.appendChild(h("span", null, "  ·  Other skill points left: "));
  pools.appendChild(h("b", subLeft < 0 ? "neg" : null, fmtN(subLeft) + " of " + fmtN(L.subSp)));
  box.appendChild(pools);

  const wrap = h("div", "ub-trees");
  L.activeTrees.forEach((t) => {
    const tr = D.trees[t];
    const weapon = tr.kind === "main";
    const card = h("div", "ub-tree");
    const hd = h("div", "ub-tree-h");
    hd.appendChild(icon(tr.tic || tr.ic));
    hd.appendChild(h("span", "ub-tree-n", tr.n));
    hd.appendChild(h("span", "tag", weapon ? "Weapon tree" : "Sub tree"));
    if (S.insctrees.indexOf(t) >= 0) {
      const x = btn("ub-x", "×", () => change((s) => { s.insctrees = s.insctrees.filter((y) => y !== t); }));
      x.setAttribute("aria-label", "Remove the " + tr.n + " tree");
      hd.appendChild(x);
    }
    card.appendChild(hd);
    const grid = h("div", "ub-skillgrid");
    tr.rows.forEach((row) => row.forEach((k) => {
      if (!k) { grid.appendChild(h("span", "ub-skill empty")); return; }
      const sk = D.skills[k];
      const on = S.learned.indexOf(k) >= 0;
      const cost = skillCostOf(k, t);
      const left = weapon ? mainLeft : subLeft;
      const why = on ? "" : sk.lv > S.lvl ? "Needs level " + sk.lv
        : sk.pre.some((p) => S.learned.indexOf(p) < 0) ? "Needs " + sk.pre.map((p) => (D.skills[p] || {}).n || p).join(", ")
        : (weapon ? cost : sk.sp) > left ? "Not enough points" : "";
      const b = btn("ub-skill" + (on ? " on" : "") + (why ? " locked" : ""), null, () => {
        focusSkill = k;
        if (on) { change((s) => { s.learned = s.learned.filter((x) => x !== k); }); return; }
        if (why) { msg(sk.n + ": " + why + ".", true); render(); return; }
        change((s) => { s.learned.push(k); }).then(() => { if (S.learned.indexOf(k) < 0) msg(sk.n + " could not be learned.", true); else msg(""); });
      });
      b.oncontextmenu = (ev) => { ev.preventDefault(); if (on) { focusSkill = k; change((s) => { s.learned = s.learned.filter((x) => x !== k); }); } };
      b.onmouseenter = () => { if (focusSkill !== k) { focusSkill = k; renderSkillDetail(); } };
      b.setAttribute("aria-pressed", on ? "true" : "false");
      b.setAttribute("aria-label", sk.n + (on ? ", learned" : why ? ", " + why : ""));
      const fr = h("span", "ub-skillframe");
      fr.appendChild(icon(sk.ic));
      b.appendChild(fr);
      b.appendChild(h("span", "ub-skill-n", sk.n));
      b.appendChild(h("span", "ub-skill-c", fmtN(weapon ? cost : sk.sp) + " pt"));
      grid.appendChild(b);
    }));
    card.appendChild(grid);
    wrap.appendChild(card);
  });
  box.appendChild(wrap);

  const tools = h("div", "ub-bar");
  tools.appendChild(h("span", "meta", "Skill points come from the level (step 4). An extra tree uses one inscription and adds " + D.consts.subTreeInscSP + " points to its pool."));
  const add = btn("secondary", addingTree ? "Close" : "Add a tree", () => { addingTree = !addingTree; render(); });
  if (!addingTree && L.inscFree < 1) { add.disabled = true; add.title = "No free inscription. Raise the level or remove an inscription."; }
  tools.appendChild(add);
  tools.appendChild(btn("secondary", "Reset trees", () => change((s) => { s.learned = []; s.extra.shelf = {}; })));
  box.appendChild(tools);
  if (addingTree) {
    const g = h("div", "ub-grid dense");
    Object.keys(D.trees).filter((t) => L.activeTrees.indexOf(t) < 0).forEach((t) => {
      const tr = D.trees[t];
      const b = cardBtn(false, L.inscFree < 1, () => { addingTree = false; change((s) => { s.insctrees.push(t); }); });
      optHead(b, tr.tic || tr.ic, tr.n, tr.kind === "main" ? "Weapon tree" : "Sub tree");
      g.appendChild(b);
    });
    box.appendChild(g);
  }
  const det = h("div", "ub-detail");
  det.id = "ub-skilldetail";
  box.appendChild(det);
  renderSkillDetail();
}
function renderSkillDetail() {
  const det = byId("ub-skilldetail");
  if (!det) return;
  det.innerHTML = "";
  const k = focusSkill || S.learned[S.learned.length - 1];
  const sk = k && D.skills[k];
  if (!sk) { det.appendChild(h("p", "meta", "Point at a skill to read it.")); return; }
  const fr = h("span", "ub-skillframe big");
  fr.appendChild(icon(sk.ic));
  det.appendChild(fr);
  const t = h("div");
  const hd = h("div", "ub-detail-h");
  hd.appendChild(h("span", "ub-detail-n", sk.n));
  const bits = [sk.ty, fmtN(sk.sp) + " points"];
  if (sk.ec) bits.push(fmtN(sk.ec) + " energy");
  if (sk.cd) bits.push(fmtN(sk.cd) + " s cooldown");
  if (sk.lv > 1) bits.push("level " + sk.lv);
  hd.appendChild(h("span", "meta", bits.join(" · ")));
  t.appendChild(hd);
  t.appendChild(h("p", "ub-detail-d", sk.tip || sk.d || ""));
  const sp = (L.stackPassives || []).find((x) => x[0] === k);
  if (sp && sp[3]) {
    const pg = D.passiveGrants[k];
    const row = h("label", "ub-chiprow");
    row.appendChild(h("span", "ub-label", "Milestones reached"));
    const sel = h("select");
    for (let i = 0; i <= sp[2]; i++) {
      const o = h("option", null, i === 0 ? "None" : i + " (" + pg.thr[i - 1] + ")");
      o.value = i;
      if (i === sp[1]) o.selected = true;
      sel.appendChild(o);
    }
    sel.onchange = () => apply("builder_set_stack_level", { build: S, skill: k, level: +sel.value });
    row.appendChild(sel);
    t.appendChild(row);
  }
  det.appendChild(t);
}

function treeInscSet() {
  const s = new Set();
  Object.keys(D.trees).forEach((t) => {
    [("AFFECTER_" + t + "Inscription"), ("AFFECTER_" + t.replace(/Magic$/, "") + "Inscription")].some((k) => {
      if (D.insc.some((x) => x.k === k)) { s.add(k); return true; }
      return false;
    });
  });
  return s;
}
function stepInsc(box) {
  const total = L.inscSlots;
  const bar = h("div", "ub-inscbar");
  const fill = [];
  S.insctrees.forEach((t) => fill.push(["tree", t]));
  S.insc.forEach((k) => fill.push(["insc", k]));
  for (let i = 0; i < total; i++) {
    const f = fill[i];
    const cell = h("span", "ub-inscslot" + (f ? " filled" : ""));
    if (f && f[0] === "tree") {
      cell.appendChild(icon(D.trees[f[1]].tic || D.trees[f[1]].ic));
      cell.title = "Extra skill tree: " + treeName(f[1]);
    } else if (f) {
      const r = D.insc.find((x) => x.k === f[1]);
      if (r) { cell.appendChild(icon(r.ic)); cell.title = r.n; }
    } else cell.title = "Free";
    if (f) {
      const x = btn("ub-x", "×", () => change((s) => {
        if (f[0] === "tree") s.insctrees = s.insctrees.filter((y) => y !== f[1]);
        else s.insc = s.insc.filter((y) => y !== f[1]);
      }));
      x.setAttribute("aria-label", "Remove");
      cell.appendChild(x);
    }
    bar.appendChild(cell);
  }
  box.appendChild(bar);
  box.appendChild(h("p", "meta", total ? (S.insctrees.length ? S.insctrees.length + " used by extra trees, " : "") + S.insc.length + " by inscriptions, " + L.inscFree + " free." : "A level 1 unit has no inscription. Raise the level in step 4."));

  const tools = h("div", "ub-chiprow");
  [["all", "All"], ["attack", "Attack"], ["survival", "Survival"], ["special", "Special"]].forEach(([v, t]) => {
    const c = btn("chip", t, () => { inscCat = v; render(); });
    c.setAttribute("aria-pressed", inscCat === v ? "true" : "false");
    tools.appendChild(c);
  });
  const q = h("input", "ub-search");
  q.type = "search"; q.placeholder = "Search"; q.value = inscSearch; q.setAttribute("aria-label", "Search inscriptions");
  q.oninput = () => { inscSearch = q.value; drawList(); };
  tools.appendChild(q);
  box.appendChild(tools);
  const list = h("div", "ub-grid dense");
  box.appendChild(list);
  const trees = treeInscSet();
  const rarOrder = { Common: 0, Uncommon: 1, Rare: 2, Legendary: 3 };
  function drawList() {
    list.innerHTML = "";
    const needle = inscSearch.trim().toLowerCase();
    D.insc.filter((r) => !trees.has(r.k) && (inscCat === "all" || r.cat === inscCat) && (!needle || r.n.toLowerCase().indexOf(needle) >= 0))
      .sort((a, b) => (rarOrder[a.r] - rarOrder[b.r]) || (a.n < b.n ? -1 : 1))
      .forEach((r) => {
        const on = S.insc.indexOf(r.k) >= 0;
        const b = cardBtn(on, !on && L.inscFree < 1, () => change((s) => {
          if (on) s.insc = s.insc.filter((x) => x !== r.k); else s.insc.push(r.k);
        }));
        optHead(b, r.ic, r.n, D.rarLabels[r.r] || r.r);
        if (r.adj && r.adj.length) b.appendChild(adjList(r.adj));
        if (r.d) b.appendChild(h("div", "ub-opt-d", r.d));
        b.classList.add("rar-" + String(r.r).toLowerCase());
        list.appendChild(b);
      });
  }
  drawList();
}

function stepSouls(box) {
  const r = race();
  box.appendChild(h("h3", "ub-h3", "Soul stone"));
  const g = h("div", "ub-grid dense");
  Object.keys(D.souls).sort((a, b) => (D.souls[a].n < D.souls[b].n ? -1 : 1)).forEach((k) => {
    const s = D.souls[k];
    const on = S.souls.indexOf(k) >= 0;
    const b = cardBtn(on, false, () => change((x) => { x.souls = on ? [] : [k]; }));
    optHead(b, s.ic, s.n);
    if (s.srcN) b.appendChild(h("div", "ub-opt-meta", "From " + s.srcN));
    if (s.d) b.appendChild(h("div", "ub-opt-d", s.d));
    g.appendChild(b);
  });
  box.appendChild(g);
  box.appendChild(h("h3", "ub-h3", "Acquired traits"));
  const g2 = h("div", "ub-grid dense");
  (D.acqOrder || D.acq.map((a) => a.k)).forEach((k) => {
    const a = D.acq.find((x) => x.k === k);
    if (!a || !acqAvail(r, a)) return;
    const on = S.acqInds.indexOf(k) >= 0;
    const b = cardBtn(on, false, () => change((x) => {
      if (on) x.acqInds = x.acqInds.filter((y) => y !== k); else x.acqInds.push(k);
    }));
    optHead(b, a.ic, a.n);
    if (a.srcN) b.appendChild(h("div", "ub-opt-meta", "From " + a.srcN));
    if (a.adj && a.adj.length) b.appendChild(adjList(a.adj));
    if (a.d) b.appendChild(h("div", "ub-opt-d", a.d));
    g2.appendChild(b);
  });
  box.appendChild(g2);
  box.appendChild(h("h3", "ub-h3", "Needs"));
  D.cond.forEach((c) => {
    const cur = (c.opts.find((o) => S.cond.indexOf(o.k) >= 0) || {}).k || "";
    box.appendChild(chipRow(c.n, [["", "No"]].concat(c.opts.map((o) => [o.k, o.n])), cur, (v) => change((x) => {
      x.cond = x.cond.filter((k) => !c.opts.some((o) => o.k === k));
      if (v) x.cond.push(v);
    })));
  });
}

const SLOT_NAME = { Primary: "Weapon", Secondary: "Off-hand", Head: "Head", Body: "Body", Accessory1: "Necklace", Accessory2: "Ring", Ammo: "Ammo" };
const SLOT_GLYPH = { Primary: "EquipmentSlot_Icon_Weapon.png", Secondary: "EquipmentSlot_Icon_Shield.png", Head: "EquipmentSlot_Icon_Helmet.png",
  Body: "EquipmentSlot_Icon_Armor.png", Accessory1: "EquipmentSlot_Icon_accessory2.png", Accessory2: "EquipmentSlot_Icon_Accessory.png", Ammo: "EquipmentSlot_Icon_Ammo.png" };
function ammoNeed() { const p = S.eq.Primary && D.items[S.eq.Primary]; return (p && p.ammo) || null; }
function slotBlocked(slot) {
  if (slot === "Secondary" && L.twoHanded) return "Two-handed weapon in hand";
  if (slot === "Ammo" && !ammoNeed()) return "The weapon takes no ammo";
  return "";
}
function itemLine(it) {
  const bits = [];
  if (it.pa) bits.push("+" + fmtN(it.pa) + " P.Atk");
  if (it.ma) bits.push("+" + fmtN(it.ma) + " M.Atk");
  if (it.hp) bits.push("+" + fmtN(it.hp) + " HP");
  if (it.th) bits.push("two-handed");
  return bits.join(" · ");
}
function stepGear(box) {
  const wrap = h("div", "ub-gear");
  const slots = h("div", "ub-slots");
  D.slots.forEach((slot) => {
    const it = S.eq[slot] && D.items[S.eq[slot]];
    const blocked = slotBlocked(slot);
    const b = btn("ub-slot" + (gearSlot === slot ? " on" : "") + (blocked ? " off" : ""), null, () => { gearSlot = slot; gearSearch = ""; render(); });
    b.setAttribute("aria-pressed", gearSlot === slot ? "true" : "false");
    const fr = h("span", "ub-slotframe");
    fr.appendChild(it ? icon(it.ic) : icon("icons/ui/" + SLOT_GLYPH[slot], "ico ph"));
    b.appendChild(fr);
    const t = h("span", "ub-slottext");
    t.appendChild(h("span", "ub-slotname", SLOT_NAME[slot]));
    t.appendChild(h("span", "ub-slotitem", it ? it.n : blocked || "Empty"));
    b.appendChild(t);
    slots.appendChild(b);
  });
  wrap.appendChild(slots);

  const side = h("div", "ub-gearside");
  const slot = gearSlot;
  const cur = S.eq[slot] && D.items[S.eq[slot]];
  const blocked = slotBlocked(slot);
  if (cur) {
    const det = h("div", "ub-itemdet rar-" + String(cur.r).toLowerCase());
    const hd = h("div", "ub-opt-h");
    hd.appendChild(icon(cur.ic));
    hd.appendChild(h("span", "ub-opt-n", cur.n));
    hd.appendChild(h("span", "tag", D.rarLabels[cur.r] || cur.r));
    hd.appendChild(btn("secondary", "Take off", () => apply("builder_set_item", { build: S, slot, item: null })));
    det.appendChild(hd);
    const line = itemLine(cur);
    if (line) det.appendChild(h("div", "ub-opt-meta", line));
    if (cur.adds && cur.adds.length) det.appendChild(adjList(cur.adds));
    if (cur.tags && cur.tags.length) {
      const n = cur.tn || 1;
      const rolls = S.tags[slot] || [];
      for (let i = 0; i < n; i++) {
        const pick = rolls[i] || null;
        const row = h("div", "ub-roll");
        row.appendChild(h("span", "ub-label", "Bonus " + (n > 1 ? i + 1 : "")));
        const sel = h("select");
        sel.setAttribute("aria-label", "Bonus " + (i + 1));
        const none = h("option", null, "No bonus"); none.value = ""; sel.appendChild(none);
        cur.tags.forEach((tg, ti) => {
          const o = h("option", null, statLabel(tg[0]) + " (" + fmtN(tg[1]) + " to " + fmtN(tg[2]) + (isMult(tg[0]) || isPct(tg[0]) ? "%" : "") + ")");
          o.value = ti;
          if (pick && pick.i === ti) o.selected = true;
          sel.appendChild(o);
        });
        const setRoll = (val) => change((s) => {
          const arr = (s.tags[slot] || []).slice();
          while (arr.length < i) arr.push(null);
          arr[i] = val;
          s.tags[slot] = arr;
        });
        sel.onchange = () => {
          if (sel.value === "") { setRoll(null); return; }
          const tg = cur.tags[+sel.value];
          setRoll({ i: +sel.value, v: tg[2] });
        };
        row.appendChild(sel);
        if (pick && cur.tags[pick.i]) {
          const tg = cur.tags[pick.i];
          const r = h("input");
          r.type = "range"; r.min = tg[1]; r.max = tg[2]; r.step = (tg[2] - tg[1]) <= 1 ? 0.01 : 1; r.value = pick.v;
          r.setAttribute("aria-label", "Bonus value");
          const out = h("b", "ub-rollval", adjText1(tg[0], pick.v));
          r.oninput = () => { out.textContent = adjText1(tg[0], +r.value); };
          r.onchange = () => setRoll({ i: pick.i, v: +r.value });
          row.appendChild(r);
          row.appendChild(out);
        }
        det.appendChild(row);
      }
    }
    side.appendChild(det);
  }
  if (blocked) {
    side.appendChild(h("p", "meta", blocked + "."));
  } else {
    const q = h("input", "ub-search");
    q.type = "search"; q.placeholder = "Search " + SLOT_NAME[slot].toLowerCase(); q.value = gearSearch;
    q.setAttribute("aria-label", "Search items");
    side.appendChild(q);
    const list = h("div", "ub-itemlist");
    side.appendChild(list);
    const need = ammoNeed();
    const rarOrder = { Common: 0, Uncommon: 1, Rare: 2, Legendary: 3 };
    const draw = () => {
      list.innerHTML = "";
      const needle = gearSearch.trim().toLowerCase();
      Object.keys(D.items).filter((k) => {
        const it = D.items[k];
        if (it.slot !== slot) return false;
        if (slot === "Ammo" && "Ammo_" + it.sub !== need) return false;
        return !needle || it.n.toLowerCase().indexOf(needle) >= 0 || String(it.sub).toLowerCase().indexOf(needle) >= 0;
      }).sort((a, b) => (D.items[a].sub < D.items[b].sub ? -1 : D.items[a].sub > D.items[b].sub ? 1 : 0) ||
        (rarOrder[D.items[a].r] - rarOrder[D.items[b].r]) || (D.items[a].n < D.items[b].n ? -1 : 1)).forEach((k) => {
        const it = D.items[k];
        const on = S.eq[slot] === k;
        const b = btn("ub-item rar-" + String(it.r).toLowerCase() + (on ? " on" : ""), null, () => apply("builder_set_item", { build: S, slot, item: k }));
        b.setAttribute("aria-pressed", on ? "true" : "false");
        b.appendChild(icon(it.ic));
        const t = h("span", "ub-item-t");
        t.appendChild(h("span", "ub-item-n", it.n));
        t.appendChild(h("span", "ub-item-m", [slot === "Primary" || slot === "Secondary" ? it.sub : "", itemLine(it)].filter(Boolean).join(" · ")));
        b.appendChild(t);
        list.appendChild(b);
      });
    };
    q.oninput = () => { gearSearch = q.value; draw(); };
    draw();
  }
  wrap.appendChild(side);
  box.appendChild(wrap);
}

function stepName(box) {
  const row = h("label", "ub-namerow");
  row.appendChild(h("span", "ub-label", "Name"));
  const n = h("input");
  n.type = "text"; n.maxLength = 24; n.value = S.name || ""; n.id = "ub-name-in";
  n.setAttribute("aria-label", "Unit name");
  n.onchange = () => change((s) => { s.name = n.value.trim() || s.name; });
  row.appendChild(n);
  box.appendChild(row);
  box.appendChild(h("h3", "ub-h3", "Portrait"));
  const g = h("div", "ub-faces");
  (D.portraits[S.race] || []).forEach((p) => {
    const on = S.portrait === p;
    const b = btn("ub-face" + (on ? " on" : ""), null, () => change((s) => { s.portrait = p; }));
    b.setAttribute("aria-pressed", on ? "true" : "false");
    b.setAttribute("aria-label", "Portrait " + p);
    b.appendChild(icon(portraitPath(p)));
    g.appendChild(b);
  });
  box.appendChild(g);
}

const STEP_FN = { 1: stepRace, 2: stepBackground, 3: stepTraits, 4: stepLevel, 5: stepTrees, 6: stepInsc, 7: stepSouls, 8: stepGear, 9: stepName };
function renderStep() {
  const [n, title, help] = STEPS[step - 1];
  byId("ub-step-title").textContent = "Step " + n + " · " + title;
  byId("ub-step-help").textContent = help;
  const box = byId("ub-step");
  const keep = box.scrollTop;
  box.innerHTML = "";
  STEP_FN[step](box);
  box.scrollTop = keep;
  byId("ub-back").disabled = step === 1;
  byId("ub-next").textContent = step < 9 ? "Next: " + STEPS[step][1] : "Save to my units";
}

// ---------- stat sheet ----------
const KEY_STATS = ["MaxHealthTotal", "PhysicalAttackPower", "MagicAttackPower", "AttackSpeed", "CriticalChance", "HitChance",
  "DodgeChance", "GuardChance", "PhysicalResistance", "MagicResistance", "MovementSpeed", "MaxEnergy"];
function statRow(e, v) {
  const tr = h("tr");
  tr.appendChild(h("td", null, e.n));
  let val;
  if (e.g === "defense") val = fmtN(resistPct(v || 0), 0) + "% less dmg";
  else if (e.k === "FinalDamageMultiplier") val = "x" + fmtN(v, 2);
  else if (e.k === "AttackSpeed") val = fmtN(v) + " / s";
  else val = fmtN(v) + (e.pm || isPct(e.k) ? " %" : "");
  const td = h("td", "num", val);
  if (e.g === "defense") td.title = e.n + " " + fmtN(v || 0, 1);
  tr.appendChild(td);
  return tr;
}
function renderSheet() {
  const box = byId("ub-sheet");
  const open = box.querySelector("details") && box.querySelector("details").open;
  const keep = box.scrollTop;
  box.innerHTML = "";
  const st = L.sheet.st;
  const maj = h("div", "ub-majors");
  D.majors.forEach((m) => {
    const c = h("div", "ub-major");
    if (D.statIcons[m]) c.appendChild(icon(D.statIcons[m]));
    c.appendChild(h("span", "ub-major-n", statLabel(m)));
    c.appendChild(h("b", "num", fmtN(L.sheet.majors[m], 1)));
    c.title = statLabel(m) + " · talent " + (D.tiers.labels[S.tal[m] || "Moderate"] || S.tal[m]);
    maj.appendChild(c);
  });
  box.appendChild(maj);
  const sv = {};
  D.sv.forEach((e) => { sv[e.k] = e; });
  const t = h("table", "ub-kv");
  KEY_STATS.forEach((k) => { if (sv[k]) t.appendChild(statRow(sv[k], st[k])); });
  box.appendChild(t);
  const prim = S.eq.Primary && D.items[S.eq.Primary];
  const est = prim && prim.mag ? L.estimatesMagic : L.estimates;
  const te = h("table", "ub-kv est");
  [["Average basic hit", fmtN(est.hit, 1)], ["Effective HP vs physical", fmtN(est.ehpP, 0)], ["Effective HP vs magic", fmtN(est.ehpM, 0)]].forEach(([a, b]) => {
    const tr = h("tr"); tr.appendChild(h("td", null, a)); tr.appendChild(h("td", "num", b)); te.appendChild(tr);
  });
  box.appendChild(te);
  const det = h("details", "ub-all");
  if (open) det.open = true;
  det.appendChild(h("summary", null, "All stats"));
  const nonfight = new Set(D.nonfight);
  D.groups.forEach(([g, label]) => {
    if (g === "major") return;
    let list = D.sv.filter((e) => e.g === g && (!nonfight.has(e.k) || e.k === "MaxWeight"));
    if (g === "aptitude") list = list.filter((e) => Math.abs(st[e.k] || 0) > 1e-9);
    if (!list.length) return;
    det.appendChild(h("div", "ub-gh", label));
    const tb = h("table", "ub-kv");
    list.forEach((e) => tb.appendChild(statRow(e, st[e.k])));
    det.appendChild(tb);
  });
  box.appendChild(det);
  const note = h("p", "ub-note");
  note.appendChild(h("span", "ub-star", "✱ "));
  note.appendChild(document.createTextNode("Passives that depend on a condition or are earned later are counted at their max, like the wiki builder."));
  box.appendChild(note);
  box.scrollTop = keep;
}

function render() {
  if (!S || !L) return;
  renderPlan();
  renderCard();
  renderStep();
  renderSheet();
}

// ---------- links, clipboard, dialogs ----------
async function copyText(text) {
  try { await navigator.clipboard.writeText(text); return true; } catch (e) { /* shown in a box below */ }
  try {
    const ta = h("textarea"); ta.value = text; document.body.appendChild(ta); ta.select();
    const ok = document.execCommand("copy"); ta.remove(); if (ok) return true;
  } catch (e) { /* box */ }
  return false;
}
// one dialog for links, names and yes / no. Resolves with the text (or "" for a plain OK), null on cancel.
function dialog(o) {
  return new Promise((resolve) => {
    const d = byId("ub-dialog");
    byId("ub-dlg-title").textContent = o.title;
    byId("ub-dlg-text").textContent = o.text || "";
    const area = byId("ub-dlg-area"), input = byId("ub-dlg-input");
    area.hidden = !o.area; area.value = o.area ? o.value || "" : ""; area.readOnly = !!o.readOnly;
    input.hidden = !o.input; input.value = o.input ? o.value || "" : "";
    byId("ub-dlg-ok").textContent = o.ok || "OK";
    byId("ub-dlg-cancel").hidden = !!o.noCancel;
    const ex = byId("ub-dlg-extra"); ex.innerHTML = ""; if (o.extra) ex.appendChild(o.extra);
    d.onclose = () => { resolve(d.returnValue === "ok" ? (o.area ? area.value : o.input ? input.value : "") : null); };
    d.returnValue = "";
    d.showModal();
    if (o.area) { area.focus(); if (o.readOnly) area.select(); } else if (o.input) { input.focus(); input.select(); }
  });
}
async function shareLink(text, what) {
  if (await copyText(text)) return true;
  await dialog({ title: what, text: "Copy this link (Cmd+C).", area: true, value: text, readOnly: true, noCancel: true, ok: "Close" });
  return false;
}
async function readClipboard() {
  try { const t = await navigator.clipboard.readText(); return /team=|#b=|dungeonsettlers/.test(t) ? t : ""; } catch (e) { return ""; }
}

async function copyLink() {
  try {
    const l = await inv("builder_encode_link", { builds: [S] });
    if (await shareLink(l.solo || l.team, "Wiki link")) msg("Wiki link copied. It opens this unit in the wiki builder.");
  } catch (e) { msg(errText(e), true); }
}
async function pasteLink(intoBuilder) {
  const text = await dialog({ title: "Paste wiki link", text: "Paste a wiki builder link (one unit or a team).", area: true, value: await readClipboard(), ok: "Open" });
  if (text == null || !text.trim()) return;
  let dec;
  try { dec = await inv("builder_decode_link", { text: text.trim() }); } catch (e) { (intoBuilder ? msg : muMsg)(errText(e), true); return; }
  const notes = [];
  if (dec.warn) notes.push("This link was made with other wiki data. Check the units.");
  if (dec.gone && dec.gone.length) notes.push("Removed from the game: " + dec.gone.join(", ") + ".");
  if (intoBuilder && dec.units.length === 1) {
    await openBuild(dec.units[0], null);
    msg(["Unit opened from the link."].concat(notes).join(" "), notes.length > 0);
    return;
  }
  const names = dec.units.map((u) => u.name || "Unit").join(", ");
  const ok = await dialog({ title: "Add to My units", text: "This link holds " + dec.units.length + " unit" + (dec.units.length > 1 ? "s" : "") + ": " + names + ". " + notes.join(" "), ok: "Add to My units" });
  if (ok == null) return;
  try {
    const added = await inv("units_import_link", { text: text.trim() });
    await loadUnits();
    showView("units");
    muMsg(added.length + " unit" + (added.length > 1 ? "s" : "") + " added to My units.");
  } catch (e) { (intoBuilder ? msg : muMsg)(errText(e), true); }
}

async function saveUnit() {
  try {
    const u = await inv("units_save", { id: unitId, build: S });
    unitId = u.id;
    S = u.build;
    savedText = JSON.stringify(S);
    await loadUnits();
    render();
    msg("Saved to My units as " + u.name + ".");
    return u;
  } catch (e) { msg(errText(e), true); return null; }
}
const BRING_HELP = "Team link copied. In the game: PvP or Foreign Expedition, Bring Units, Paste Wiki Link.";
async function bring(ids, name, say) {
  try {
    const ex = await inv("units_export", { unitIds: ids, name: name || null });
    const copied = await shareLink(ex.link, "Team link");
    say((copied ? BRING_HELP : "") + (ex.notes && ex.notes.length ? " " + ex.notes.join(" ") : ""), false);
  } catch (e) { say(errText(e), true); }
}

// ---------- open / new ----------
async function openBuild(build, id) {
  const b = await inv("builder_validate", { build });
  L = await inv("builder_compute", { build: b });
  S = b;
  unitId = id;
  savedText = id ? JSON.stringify(S) : null;
  step = 1; focusSkill = null; addingTree = false; gearSlot = "Primary";
  msg("");
  showView("builder");
  render();
}
async function newUnit() {
  const b = await inv("builder_new_unit", { race: null });
  await openBuild(b, null);
}

// ---------- My Units ----------
async function loadUnits() {
  try { [units, teams] = await inv("units_list"); } catch (e) { muMsg(errText(e), true); units = []; teams = []; }
  for (const id of Array.from(picked)) if (!units.some((u) => u.id === id)) picked.delete(id);
  renderUnits();
}
function unitSub(b) {
  const r = D.races.find((x) => x.k === b.race);
  return [r && r.n, b.bg && D.bg[b.bg] && D.bg[b.bg].n, "LV. " + b.lvl].filter(Boolean).join(" · ");
}
function renderUnits() {
  if (!D) return;
  byId("mu-count").textContent = units.length ? units.length + " saved unit" + (units.length > 1 ? "s" : "") : "No saved unit yet. Build one in the Unit Builder, then press Save to my units.";
  const ul = byId("mu-units");
  ul.innerHTML = "";
  units.forEach((u) => {
    const li = h("li", "mu-unit" + (picked.has(u.id) ? " on" : ""));
    const cb = h("input");
    cb.type = "checkbox"; cb.checked = picked.has(u.id);
    cb.setAttribute("aria-label", "Pick " + u.name);
    cb.onchange = () => {
      if (cb.checked && picked.size >= 8) { cb.checked = false; muMsg("A team holds 8 units at most.", true); return; }
      if (cb.checked) picked.add(u.id); else picked.delete(u.id);
      renderUnits();
    };
    li.appendChild(cb);
    li.appendChild(icon(portraitPath(u.build.portrait), "mu-face"));
    const t = h("div", "mu-text");
    t.appendChild(h("span", "mu-name", u.name));
    t.appendChild(h("span", "meta", unitSub(u.build)));
    li.appendChild(t);
    const acts = h("div", "actions");
    acts.appendChild(btn("secondary", "Open", () => openBuild(u.build, u.id)));
    acts.appendChild(btn("secondary", "Rename", async () => {
      const n = await dialog({ title: "Rename", input: true, value: u.name, ok: "Rename" });
      if (n == null || !n.trim()) return;
      try { await inv("units_rename", { id: u.id, name: n.trim() }); await loadUnits(); } catch (e) { muMsg(errText(e), true); }
    }));
    acts.appendChild(btn("secondary", "Duplicate", async () => {
      try { await inv("units_duplicate", { id: u.id }); await loadUnits(); } catch (e) { muMsg(errText(e), true); }
    }));
    acts.appendChild(btn("secondary", "Delete", async () => {
      const ok = await dialog({ title: "Delete " + u.name + "?", text: "The unit leaves My units and its teams. The file keeps one backup (units.json.bak).", ok: "Delete" });
      if (ok == null) return;
      try {
        await inv("units_delete", { id: u.id });
        if (unitId === u.id) { unitId = null; savedText = null; }
        await loadUnits();
      } catch (e) { muMsg(errText(e), true); }
    }));
    li.appendChild(acts);
    ul.appendChild(li);
  });
  const ids = Array.from(picked);
  byId("mu-picked").textContent = ids.length ? ids.length + " picked: " + ids.map((id) => (units.find((u) => u.id === id) || {}).name).join(", ") : "No unit picked.";
  ["mu-team-save", "mu-team-copy", "mu-bring"].forEach((id) => { byId(id).disabled = !ids.length; });
  const tl = byId("mu-teams");
  tl.innerHTML = "";
  teams.forEach((t) => {
    const li = h("li", "mu-team");
    const hd = h("div", "mu-team-h");
    hd.appendChild(h("span", "mu-name", t.name));
    hd.appendChild(h("span", "meta", t.unit_ids.length + " units"));
    li.appendChild(hd);
    const faces = h("div", "mu-faces");
    t.unit_ids.forEach((id) => {
      const u = units.find((x) => x.id === id);
      if (!u) return;
      const f = icon(portraitPath(u.build.portrait), "mu-face small");
      f.title = u.name;
      faces.appendChild(f);
    });
    li.appendChild(faces);
    const acts = h("div", "actions");
    acts.appendChild(btn("secondary", "Bring", () => bring(t.unit_ids, t.name, muMsg)));
    acts.appendChild(btn("secondary", "Pick these", () => { picked.clear(); t.unit_ids.forEach((id) => picked.add(id)); renderUnits(); }));
    acts.appendChild(btn("secondary", "Delete", async () => {
      const ok = await dialog({ title: "Delete the team " + t.name + "?", text: "The units stay in My units.", ok: "Delete" });
      if (ok == null) return;
      try { await inv("teams_delete", { id: t.id }); await loadUnits(); } catch (e) { muMsg(errText(e), true); }
    }));
    li.appendChild(acts);
    tl.appendChild(li);
  });
}

// ---------- views ----------
function showView(v) {
  ["launcher", "builder", "units"].forEach((x) => { byId("view-" + x).hidden = x !== v; });
  document.querySelectorAll(".tab").forEach((t) => t.setAttribute("aria-pressed", t.dataset.view === v ? "true" : "false"));
  if (v === "units") renderUnits();
}

async function boot() {
  document.querySelectorAll(".tab").forEach((t) => { t.onclick = () => showView(t.dataset.view); });
  byId("ub-back").onclick = () => { if (step > 1) { step--; focusSkill = null; addingTree = false; render(); } };
  byId("ub-next").onclick = () => { if (step < 9) { step++; focusSkill = null; addingTree = false; render(); } else saveUnit(); };
  byId("ub-new").onclick = () => newUnit();
  byId("mu-new").onclick = () => newUnit();
  byId("ub-copy").onclick = copyLink;
  byId("ub-paste").onclick = () => pasteLink(true);
  byId("mu-import").onclick = () => pasteLink(false);
  byId("ub-save").onclick = saveUnit;
  byId("ub-bring").onclick = async () => {
    const u = await saveUnit();
    if (u) await bring([u.id], u.name, msg);
  };
  byId("mu-bring").onclick = () => bring(Array.from(picked), "team", muMsg);
  byId("mu-team-copy").onclick = async () => {
    try {
      const l = await inv("builder_encode_link", { builds: Array.from(picked).map((id) => units.find((u) => u.id === id).build) });
      if (await shareLink(l.team, "Team link")) muMsg("Team link copied. It opens the team in the wiki builder.");
    } catch (e) { muMsg(errText(e), true); }
  };
  byId("mu-team-save").onclick = async () => {
    const n = await dialog({ title: "Save as team", input: true, value: "Team " + (teams.length + 1), ok: "Save team" });
    if (n == null || !n.trim()) return;
    try { await inv("teams_create", { name: n.trim(), unitIds: Array.from(picked) }); await loadUnits(); muMsg("Team saved."); } catch (e) { muMsg(errText(e), true); }
  };
  try {
    CAT = await inv("builder_catalog");
    D = CAT.dsb;
    byId("game-line").textContent = "Builder data: game " + CAT.gameVersion + " · build " + CAT.gameBuild;
    await loadUnits();
    await newUnit();
    showView("launcher");
  } catch (e) {
    byId("ub-step").textContent = "The Unit Builder could not start: " + errText(e);
  }
}
window.UB = { showView: (v) => showView(v), state: () => ({ S, L, unitId, step }) };
boot();
})();
