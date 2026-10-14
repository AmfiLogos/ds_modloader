// UI of the launcher. All work happens in Rust (dsml-core); this file only shows the state.
const invoke = (cmd, args) => window.__TAURI__.core.invoke(cmd, args);
const $ = (id) => document.getElementById(id);
let busy = false;
let which = { kind: "new" };
let last = null;

function el(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
}

function button(cls, text, onclick) {
  const b = el("button", cls, text);
  b.type = "button";
  b.onclick = onclick;
  return b;
}

function say(text, warn) {
  const m = $("message");
  m.textContent = text || "";
  m.classList.toggle("warn-text", !!warn);
}

async function call(cmd, args) {
  try { const st = await invoke(cmd, Object.assign({ which }, args || {})); render(st); return st; }
  catch (e) { say(String(e), true); }
}

const sameWhich = (a, b) => a.kind === b.kind && (a.kind === "new" || a.guid === b.guid);

function planText(st) {
  const d = st.decision;
  if (!d) return [st.index_error ? "No checked mod list yet. Press Check for updates." : "", true];
  const p = d.plan;
  switch (p.kind) {
    case "no_game": return ["Dungeon Settlers is not installed by Steam on this computer.", true];
    case "steam_updating": return ["Steam is updating the game. Wait until it is done.", true];
    case "setup": return [`Play makes your modded copy of build ${p.build}. Steam's own game is not changed.`, false];
    case "blocked": return [`No mod version for game build ${p.build} yet: ${p.missing.join(", ")}.`, true];
    case "current": return [`This world plays on build ${p.build}. Ready to play.`, false];
    case "pinned": return [`Steam has build ${p.steam_build}. This world stays on build ${p.build} until these mods are ready: ${p.waiting_for.join(", ")}.`, true];
    case "switch": return [`Steam has build ${p.to} and this world's mods are ready for it. Play moves the world to ${p.to}. The copy of ${p.from} is kept.`, false];
    case "stuck": return [`The copy of build ${p.build} is gone. Steam has ${p.steam_build}. Waiting for: ${p.waiting_for.join(", ")}.`, true];
  }
  return ["", false];
}

function renderWorlds(st) {
  const list = $("world-list");
  list.replaceChildren();
  const rows = [{ w: { kind: "new" }, name: "New world", meta: "mods for the next new campaign" }]
    .concat(st.worlds.map((w) => ({ w: { kind: "world", guid: w.guid }, name: w.name || w.guid,
      meta: (w.build ? `build ${w.build}` : "") + (w.custom_difficulty ? " · custom difficulty" : "") })));
  for (const r of rows) {
    const li = el("li");
    const b = button("", r.name, () => { which = r.w; load(); });
    b.setAttribute("aria-pressed", sameWhich(r.w, which) ? "true" : "false");
    b.append(el("span", "meta", r.meta));
    li.append(b);
    list.append(li);
  }
}

function renderMods(st, pinnedBuild) {
  const list = $("mod-list");
  list.replaceChildren();
  const isWorld = which.kind === "world";
  for (const m of st.mods) {
    const li = el("li");
    const sw = el("label", "switch");
    const cb = el("input");
    cb.type = "checkbox";
    cb.checked = m.enabled;
    cb.disabled = !!m.via;
    cb.setAttribute("aria-label", `${m.name} on or off for this world`);
    cb.onchange = () => call("set_mod", { id: m.id, on: cb.checked });
    sw.append(cb);
    const name = el("span", "mod-name", `${m.name} `);
    name.append(el("span", "meta", m.for_pinned || m.for_steam || m.latest));
    const tags = el("span", "tags");
    if (m.via) tags.append(el("span", "tag", m.via === "group" ? "from a group" : "difficulty"));
    tags.append(el("span", m.scope === "client" ? "tag client" : "tag",
      m.scope === "client" ? "only for you" : "every player needs it"));
    const ok = m.for_pinned || (!pinnedBuild && m.for_steam);
    const b = pinnedBuild || st.steam.build || "?";
    tags.append(el("span", ok ? "tag yes" : "tag no", ok ? `build ${b}: yes` : `build ${b}: no`));
    li.append(sw, name, tags, el("span", "mod-desc", m.description));
    list.append(li);
  }
  if (!st.mods.length) list.append(el("li", "meta", "No mods in the list yet."));
  $("mods-title").textContent = isWorld ? "Mods of this world" : "Mods for a new world";
}

function renderGroups(st) {
  const box = $("group-list");
  box.replaceChildren();
  const names = Object.keys(st.state.groups);
  const applied = which.kind === "world" ? ((st.state.worlds[which.guid] || {}).groups || []) : [];
  for (const n of names) {
    const members = st.state.groups[n];
    const on = which.kind === "world" ? applied.includes(n)
      : members.every((id) => st.state.new_world.includes(id));
    const b = button("chip", `${n} (${members.length})`, () => call("apply_group", { name: n, on: !on }));
    b.setAttribute("aria-pressed", on ? "true" : "false");
    b.setAttribute("aria-label", `Group ${n}: ${members.join(", ")}. ${on ? "Take it off" : "Apply it"} for this world`);
    box.append(b);
  }
  if (!names.length) box.append(el("span", "meta", "No groups yet."));
}

function renderDifficulty(st) {
  $("custom-toggle").checked = st.state.custom_difficulties;
  const list = $("difficulty-list");
  list.replaceChildren();
  const isNew = which.kind === "new";
  const world = isNew ? null : st.worlds.find((w) => w.guid === which.guid);
  const pickedMod = isNew ? (st.difficulties.find((d) => d.mod_id && st.state.new_world.includes(d.mod_id)) || {}).mod_id : null;
  for (const d of st.difficulties) {
    const li = el("li", (d.locked ? "locked " : "") + (d.mod_id ? "custom " : ""));
    const picked = world ? world.combat_difficulty === d.value : (d.mod_id ? pickedMod === d.mod_id : false);
    if (picked) li.classList.add("picked");
    const label = d.name + (d.locked ? " (locked)" : "");
    if (isNew && d.mod_id && !d.locked) {
      li.append(button("", label, () => call("set_new_world_difficulty", { modId: pickedMod === d.mod_id ? null : d.mod_id })));
    } else {
      li.textContent = label;
    }
    if (d.lock_reason) li.title = d.lock_reason;
    list.append(li);
  }
  const unlock = st.devastation_unlocked === true ? "Devastation is unlocked in your game."
    : "Devastation unlocks in the game when you clear Very Hard.";
  $("difficulty-note").textContent = isNew
    ? `${unlock} You pick the game's own difficulties when you start the campaign. Click a custom difficulty to use it for the new world.`
    : `${unlock} A world keeps the difficulty it was started with.`;
}

function render(st) {
  last = st;
  if (!sameWhich(st.which, which)) which = st.which;
  renderWorlds(st);
  const p = st.decision && st.decision.plan;
  const pinned = which.kind === "world" ? ((st.state.worlds[which.guid] || {}).build || st.state.active_build) : st.state.active_build;
  $("steam-build").textContent = st.steam.found ? st.steam.build + (st.steam.updating ? " (updating)" : "") : "not found";
  $("pinned-build").textContent = pinned || "-";
  $("copies").textContent = st.copies.length ? st.copies.join(", ") : "none";

  const [text, warn] = planText(st);
  const plan = $("plan");
  plan.textContent = text;
  plan.classList.toggle("warn", warn);
  plan.hidden = !text;

  const actions = $("plan-actions");
  actions.replaceChildren();
  const waiting = p && (p.waiting_for || p.missing);
  const ids = (waiting || []).filter((x) => !x.startsWith("("));
  if (ids.length) {
    actions.append(button("secondary", "Turn these mods off for this world", async () => {
      for (const id of ids) await invoke("set_mod", { which, id, on: false });
      load();
    }));
  }
  const rem = $("removable");
  rem.replaceChildren();
  for (const b of (st.decision && st.decision.removable) || []) {
    rem.append(button("secondary", `Remove old copy ${b}`, () => {
      if (confirm(`Remove the copy of build ${b}? You can then no longer join friends who are still on it.`)) call("remove_copy", { build: b });
    }));
  }

  $("index-line").textContent = st.index_error ? st.index_error : (st.index_seq ? `Mod list ${st.index_seq}, signature checked.` : "");
  $("index-line").classList.toggle("warn-text", !!st.index_error);
  const w = which.kind === "world" ? st.state.worlds[which.guid] : null;
  $("rule-row").hidden = !w;
  if (w) {
    const rule = w.match_rule || "mods";
    $("rule-mods").setAttribute("aria-pressed", rule === "mods" ? "true" : "false");
    $("rule-vanilla").setAttribute("aria-pressed", rule === "vanilla" ? "true" : "false");
    const gp = st.mods.filter((m) => m.enabled && m.scope !== "client").map((m) => m.name);
    $("rule-note").textContent = rule === "vanilla"
      ? (gp.length ? `Vanilla only: turn off ${gp.join(", ")} to host.` : "Nobody may use gameplay mods. Mods only for yourself are fine.")
      : "Players get your gameplay mods from the signed list when they join.";
    $("rule-note").classList.toggle("warn-text", rule === "vanilla" && gp.length > 0);
  }
  renderMods(st, pinned);
  renderGroups(st);
  renderDifficulty(st);
  const canPlay = p && ["setup", "current", "pinned", "switch"].includes(p.kind);
  $("play").disabled = busy || !canPlay;
  $("play").textContent = which.kind === "world" ? "Play this world" : "Play (new world)";
}

async function load() { await call("status"); }

$("refresh").onclick = async () => {
  $("refresh").disabled = true;
  say("Checking the mod list...");
  const st = await call("refresh_index");
  if (st) say(st.index_error ? "" : "Mod list is up to date.");
  $("refresh").disabled = false;
};

$("rule-mods").onclick = () => call("set_match_rule", { rule: "mods" });
$("rule-vanilla").onclick = () => call("set_match_rule", { rule: "vanilla" });

$("custom-toggle").onchange = (e) => call("set_custom_difficulties", { on: e.target.checked });

$("save-group").onclick = () => { $("group-name").value = ""; $("group-dialog").showModal(); };
$("group-dialog").addEventListener("close", () => {
  if ($("group-dialog").returnValue !== "ok" || !last) return;
  const mods = last.mods.filter((m) => m.enabled && !m.via).map((m) => m.id);
  if (!mods.length) { say("Turn on at least one mod first.", true); return; }
  call("save_group", { name: $("group-name").value, mods });
});

$("play").onclick = async () => {
  busy = true; $("play").disabled = true;
  say("Preparing the copy of the game for this world. The first time takes about a minute.");
  try {
    const r = await invoke("play", { which });
    say(`Game started (build ${r.build}).`);
  } catch (e) { say(String(e), true); }
  busy = false;
  load();
};

load().then(() => $("refresh").click());
setInterval(() => { if (!busy) $("refresh").click(); }, 3 * 60 * 60 * 1000);
