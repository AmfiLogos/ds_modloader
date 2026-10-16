#!/usr/bin/env python3
"""Headless UI smoke of the Unit Builder against the real commands (examples/ui_bridge.rs). No visible window.

  cargo build -p ds-mod-launcher --example ui_bridge
  target/debug/examples/ui_bridge 18931 test_env/ui_bridge &
  python3 src-tauri/tests/ui_smoke.py 18931 builder/shots

Checks: pick a race, change the level, the sheet updates, copy the wiki link and paste it back (same unit),
save to My units, bring (team link). Screenshots of every step go to the shots folder."""
import json, sys, os
from playwright.sync_api import sync_playwright

if not os.path.isfile(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "ui", "bicons", "manifest.txt")):
    sys.exit("ui/bicons/ is missing: the Unit Builder icons are game art and are not in git. "
             "Run: python3 builder/gen/extract_icons.py (see BUILDING.md, Unit Builder icons)")

port = sys.argv[1] if len(sys.argv) > 1 else "18931"
shots = sys.argv[2] if len(sys.argv) > 2 else "builder/shots"
os.makedirs(shots, exist_ok=True)
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
fails = []


def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        fails.append(what)


def sheet(page):
    return page.evaluate("() => { const s = UB.state(); return {hp: s.L.sheet.st.MaxHealthTotal, str: s.L.sheet.majors.Strength, race: s.S.race, lvl: s.S.lvl, name: s.S.name} }")


with sync_playwright() as p:
    b = p.chromium.launch(executable_path=CHROME, headless=True, args=["--disable-gpu"])
    ctx = b.new_context(viewport={"width": 1240, "height": 800}, permissions=["clipboard-read", "clipboard-write"])
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"http://127.0.0.1:{port}/")
    page.wait_for_function("() => window.UB && UB.state().L")
    page.click(".tab[data-view=builder]")
    page.wait_for_timeout(300)
    page.screenshot(path=f"{shots}/step1_race.png")

    before = sheet(page)
    page.click(".ub-opt:has-text('Elf')")
    page.wait_for_function("() => UB.state().S.race === 'AFFECTER_Elf'")
    after_race = sheet(page)
    check(after_race["race"] == "AFFECTER_Elf", "race picked: Elf")
    check(after_race["str"] != before["str"] or after_race["hp"] != before["hp"], f"sheet follows the race (STR {before['str']} -> {after_race['str']})")
    check(page.inner_text(".ub-card .ub-sub").startswith("Elf"), "unit card says Elf")

    page.click(".ub-stepbtn:has-text('Background')")
    page.screenshot(path=f"{shots}/step2_background.png")
    page.click(".ub-stepbtn:has-text('Traits')")
    page.click(".ub-grid .ub-opt >> nth=0")
    page.wait_for_function("() => UB.state().S.inds.length === 1")
    page.screenshot(path=f"{shots}/step3_traits.png")

    page.click(".ub-stepbtn:has-text('Level and talents')")
    hp5 = sheet(page)["hp"]
    shown5 = page.inner_text(".ub-kv tr:first-child td.num")
    page.fill("#ub-lvl", "10")
    page.dispatch_event("#ub-lvl", "change")
    page.wait_for_function("() => UB.state().S.lvl === 10")
    hp10 = sheet(page)["hp"]
    shown10 = page.inner_text(".ub-kv tr:first-child td.num")
    check(hp10 > hp5, f"level 5 -> 10 raises Max Health ({hp5} -> {hp10})")
    check(shown10 != shown5 and shown10 == str(round(hp10)) if hp10 >= 100 else True, f"sheet cell shows it ({shown5} -> {shown10})")
    page.click(".ub-talgrid .ub-tsq:has-text('Genius') >> nth=0")
    page.wait_for_function("() => UB.state().S.tal.Strength === 'Genius'")
    page.screenshot(path=f"{shots}/step4_level.png")

    page.click(".ub-stepbtn:has-text('Skill trees')")
    n0 = len(page.evaluate("() => UB.state().S.learned"))
    page.click(".ub-skill:not(.locked):not(.on):not(.empty) >> nth=0")
    page.wait_for_function(f"() => UB.state().S.learned.length > {n0}")
    check(True, "learned a skill")
    page.hover(".ub-skill.on >> nth=0")
    page.screenshot(path=f"{shots}/step5_trees.png")

    page.click(".ub-stepbtn:has-text('Inscriptions')")
    page.click(".ub-grid .ub-opt:not(.off) >> nth=0")
    page.wait_for_function("() => UB.state().S.insc.length === 1")
    page.screenshot(path=f"{shots}/step6_inscriptions.png")

    page.click(".ub-stepbtn:has-text('Soul stones')")
    page.click(".ub-grid .ub-opt >> nth=0")
    page.wait_for_function("() => UB.state().S.souls.length === 1")
    page.screenshot(path=f"{shots}/step7_souls.png")

    page.click(".ub-stepbtn:has-text('Gear')")
    page.click(".ub-item >> nth=0")
    page.wait_for_function("() => !!UB.state().S.eq.Primary")
    page.click(".ub-slot:has-text('Necklace')")
    page.click(".ub-item:has-text('Silver Necklace')")
    page.wait_for_function("() => !!UB.state().S.eq.Accessory1")
    page.select_option(".ub-roll select >> nth=0", index=4)
    page.wait_for_function("() => (UB.state().S.tags.Accessory1 || []).length > 0")
    page.fill(".ub-roll input[type=range]", "7")
    page.dispatch_event(".ub-roll input[type=range]", "change")
    page.wait_for_function("() => UB.state().S.tags.Accessory1[0].v === 7")
    check(True, "item bonus roll set (Critical Chance 7)")
    page.screenshot(path=f"{shots}/step8_gear.png")

    page.click(".ub-stepbtn:has-text('Name and portrait')")
    page.fill("#ub-name-in", "Smoke Elf")
    page.dispatch_event("#ub-name-in", "change")
    page.wait_for_function("() => UB.state().S.name === 'Smoke Elf'")
    page.click(".ub-face >> nth=3")
    page.wait_for_timeout(200)
    page.screenshot(path=f"{shots}/step9_name.png")

    # copy -> paste round trip
    built = page.evaluate("() => JSON.stringify(UB.state().S)")
    page.click("#ub-copy")
    page.wait_for_function("() => /copied/.test(document.getElementById('ub-msg').textContent)")
    link = page.evaluate("() => navigator.clipboard.readText()")
    check("#b=~" in link, f"wiki link copied ({link[:60]}...)")
    page.click("#ub-new")
    page.wait_for_function("() => UB.state().S.name !== 'Smoke Elf'")
    page.click("#ub-paste")
    page.wait_for_selector("#ub-dialog[open]")
    check(page.input_value("#ub-dlg-area") == link, "paste box is pre-filled from the clipboard")
    page.screenshot(path=f"{shots}/paste_dialog.png")
    page.click("#ub-dlg-ok")
    page.wait_for_function("() => UB.state().S.name === 'Smoke Elf'")
    back = json.loads(page.evaluate("() => JSON.stringify(UB.state().S)"))
    orig = json.loads(built)
    keys = ["race", "bg", "inds", "lvl", "tal", "learned", "insc", "insctrees", "souls", "eq", "name", "portrait", "offer", "roll"]
    diff = [k for k in keys if back.get(k) != orig.get(k)]
    check(not diff, "link round trip keeps the unit" + (f" (differs: {diff})" if diff else ""))
    rolls_o, rolls_b = orig.get("tags", {}).get("Accessory1"), back.get("tags", {}).get("Accessory1")
    check(rolls_o == rolls_b, f"link round trip keeps the item roll ({rolls_o} / {rolls_b})")

    page.click("#ub-save")
    page.wait_for_function("() => !!UB.state().unitId")
    check("Saved" in page.inner_text("#ub-msg"), "saved to My units")
    page.click("#ub-bring")
    page.wait_for_function("() => /Team link copied|Paste Wiki Link|refus|cannot|not/i.test(document.getElementById('ub-msg').textContent)")
    team = page.evaluate("() => navigator.clipboard.readText()")
    check("?team=" in team, f"bring copies the team link: {page.inner_text('#ub-msg')}")
    page.screenshot(path=f"{shots}/builder_full.png")

    page.click(".tab[data-view=units]")
    page.wait_for_selector(".mu-unit")
    page.click(".mu-unit input[type=checkbox] >> nth=0")
    page.screenshot(path=f"{shots}/my_units.png")

    page.click(".tab[data-view=launcher]")
    page.wait_for_timeout(200)
    page.screenshot(path=f"{shots}/launcher_still_there.png")
    check(page.is_visible("#play"), "launcher screen still there")

    check(not errors, "no page errors " + "; ".join(errors[:3]))
    b.close()

print("FAILED: %d" % len(fails) if fails else "ALL OK")
sys.exit(1 if fails else 0)
