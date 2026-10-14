// UI of the launcher. All work happens in Rust (dsml-core); this file only shows the state.
const invoke = (cmd, args) => window.__TAURI__.core.invoke(cmd, args);
const $ = (id) => document.getElementById(id);
let busy = false;

function el(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
}

function say(text, warn) {
  const m = $("message");
  m.textContent = text || "";
  m.classList.toggle("warn-text", !!warn);
}

function planText(st) {
  const d = st.decision;
  if (!d) return [st.index_error ? "No checked mod list yet. Press Check for updates." : "", true];
  const p = d.plan;
  switch (p.kind) {
    case "no_game": return ["Dungeon Settlers is not installed by Steam on this computer.", true];
    case "steam_updating": return ["Steam is updating the game. Wait until it is done.", true];
    case "setup": return [`First start: Play makes your modded copy of build ${p.build}. Steam's own game is not changed.`, false];
    case "blocked": return [`No mod version for game build ${p.build} yet: ${p.missing.join(", ")}.`, true];
    case "current": return [`Your copy is on build ${p.build}. Ready to play.`, false];
    case "pinned": return [`Steam has build ${p.steam_build}. You stay on build ${p.build} until these mods are ready: ${p.waiting_for.join(", ")}.`, true];
    case "switch": return [`Steam has build ${p.to} and your mods are ready for it. Play moves you to ${p.to}. The copy of ${p.from} is kept.`, false];
    case "stuck": return [`The copy of build ${p.build} is gone. Steam has ${p.steam_build}. Waiting for: ${p.waiting_for.join(", ")}.`, true];
  }
  return ["", false];
}

function render(st) {
  $("steam-build").textContent = st.steam.found ? st.steam.build + (st.steam.updating ? " (updating)" : "") : "not found";
  $("active-build").textContent = st.state.active_build || "-";
  $("copies").textContent = st.copies.length ? st.copies.join(", ") : "none";

  const [text, warn] = planText(st);
  const plan = $("plan");
  plan.textContent = text;
  plan.classList.toggle("warn", warn);
  plan.hidden = !text;

  const actions = $("plan-actions");
  actions.replaceChildren();
  const p = st.decision && st.decision.plan;
  const waiting = p && (p.waiting_for || p.missing);
  if (waiting && waiting.length) {
    const ids = waiting.filter((x) => !x.startsWith("("));
    if (ids.length) {
      const b = el("button", "secondary", "Turn these mods off");
      b.type = "button";
      b.onclick = async () => { for (const id of ids) await invoke("set_enabled", { id, on: false }); load(); };
      actions.append(b);
    }
  }

  const rem = $("removable");
  rem.replaceChildren();
  for (const b of (st.decision && st.decision.removable) || []) {
    const btn = el("button", "secondary", `Remove old copy ${b}`);
    btn.type = "button";
    btn.onclick = async () => {
      if (!confirm(`Remove the copy of build ${b}? You can then no longer join friends who are still on it.`)) return;
      try { render(await invoke("remove_copy", { build: b })); } catch (e) { say(String(e), true); }
    };
    rem.append(btn);
  }

  $("index-line").textContent = st.index_error ? st.index_error : (st.index_seq ? `Mod list ${st.index_seq}, signature checked.` : "");
  $("index-line").classList.toggle("warn-text", !!st.index_error);

  const list = $("mod-list");
  list.replaceChildren();
  const playBuild = (p && (p.to || p.build)) || st.state.active_build || st.steam.build;
  for (const m of st.mods) {
    const li = el("li");
    const sw = el("label", "switch");
    const cb = el("input");
    cb.type = "checkbox";
    cb.checked = m.enabled;
    cb.setAttribute("aria-label", `${m.name} on or off`);
    cb.onchange = async () => {
      try { render(await invoke("set_enabled", { id: m.id, on: cb.checked })); } catch (e) { say(String(e), true); }
    };
    sw.append(cb);
    const name = el("span", "mod-name", `${m.name} `);
    name.append(el("span", "meta", m.for_active || m.for_steam || m.latest));
    const tags = el("span", "tags");
    if (m.sim) tags.append(el("span", "tag", "multiplayer: must match"));
    const ok = playBuild === st.steam.build ? m.for_steam : (m.for_active || m.for_steam);
    tags.append(el("span", ok ? "tag yes" : "tag no", ok ? `build ${playBuild}: yes` : `build ${playBuild || "?"}: no`));
    li.append(sw, name, tags, el("span", "mod-desc", m.description));
    list.append(li);
  }
  if (!st.mods.length) list.append(el("li", "meta", "No mods in the list yet."));

  const canPlay = p && ["setup", "current", "pinned", "switch"].includes(p.kind);
  $("play").disabled = busy || !canPlay;
}

async function load() {
  try { render(await invoke("status")); } catch (e) { say(String(e), true); }
}

$("refresh").onclick = async () => {
  $("refresh").disabled = true;
  say("Checking the mod list...");
  try { const st = await invoke("refresh_index"); render(st); say(st.index_error ? "" : "Mod list is up to date."); }
  catch (e) { say(String(e), true); }
  $("refresh").disabled = false;
};

$("play").onclick = async () => {
  busy = true; $("play").disabled = true;
  say("Preparing your copy of the game. The first time takes about a minute.");
  try {
    const r = await invoke("play");
    say(`Game started (build ${r.build}).`);
  } catch (e) { say(String(e), true); }
  busy = false;
  load();
};

load().then(() => $("refresh").click());
setInterval(() => { if (!busy) $("refresh").click(); }, 3 * 60 * 60 * 1000);
