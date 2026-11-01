#!/usr/bin/env python3
"""Guard of the ROADMAP.md release check (changelog.py roadmap; PLAN_v013 item 12, owner 2026-10-16: "a clear to-do on all
GitHub projects ... so that anybody can know what's next"). Breaks it catches:
 - a release ships an item that its roadmap still lists under "Now" / "Next" (players read a stale to-do);
 - a "Now" item without an id (the shipped check could never see it);
 - a roadmap older than the newest release (nobody moved what shipped);
 - the id comment of a CHANGELOG bullet shows up in the game's What's new window.
Also: the three real ROADMAP.md files pass. Exit 0 = PASS.  usage: roadmap_test.py"""
import os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "changelog.py")
sys.path.insert(0, HERE)
import changelog as C

CL = """# Changelog

## Thing (kit)

### Unreleased (0.2.1)

- Coming soon. <!-- roadmap: soon-item -->

### 0.2.0 (2026-11-05)

- The size buttons hide. <!-- roadmap: shipped-item -->
"""
RM = """# Roadmap

Updated: {date}

## Now

{now}

## Done

- old
"""
fails = 0


def run(cl, rm):
    d = tempfile.mkdtemp(prefix="roadmap_test.")
    open(os.path.join(d, "CHANGELOG.md"), "w").write(cl)
    open(os.path.join(d, "ROADMAP.md"), "w").write(rm)
    p = subprocess.run([sys.executable, "-I", TOOL, "roadmap", "--file", os.path.join(d, "CHANGELOG.md"),
                        "--roadmap", os.path.join(d, "ROADMAP.md")], capture_output=True, text=True)
    return p.returncode, p.stdout


def case(name, want_ok, rm, needle=None, cl=CL):
    global fails
    rc, out = run(cl, rm)
    ok = (rc == 0) == want_ok and (needle is None or needle in out)
    print(("PASS " if ok else "FAIL ") + name + ("" if ok else f" (rc {rc}): {out.strip()}"))
    fails += not ok


case("clean roadmap passes", True, RM.format(date="2026-11-05", now="- A thing. <!-- id: soon-item -->"))
case("unreleased item may stay under Now", True, RM.format(date="2026-11-05", now="- Soon. <!-- id: soon-item -->"))
case("shipped item still under Now fails", False,
     RM.format(date="2026-11-05", now="- Size buttons hide. <!-- id: shipped-item -->"), "'shipped-item' is still under 'Now'")
case("shipped item under Next fails", False,
     RM.format(date="2026-11-05", now="- x <!-- id: a -->\n\n## Next\n\n- y <!-- id: shipped-item -->"), "still under 'Next'")
case("Now item without id fails", False, RM.format(date="2026-11-05", now="- A thing with no id."), "has no id")
case("roadmap older than the newest release fails", False,
     RM.format(date="2026-11-04", now="- A thing. <!-- id: soon-item -->"), "is older than the newest release")
case("no Updated line fails", False, "# Roadmap\n\n## Now\n\n- x <!-- id: a -->\n", "no line 'Updated:")

# the in-game What's new text never shows the id comment
d = tempfile.mkdtemp(prefix="roadmap_test.")
open(os.path.join(d, "CHANGELOG.md"), "w").write(CL)
secs, errs = C.parse(os.path.join(d, "CHANGELOG.md"))
shown = C.display(secs["kit"]["entries"][1]["body"])
ok = "<!--" not in shown and shown == "- The size buttons hide."
print(("PASS " if ok else "FAIL ") + "What's new text has no id comment" + ("" if ok else f": {shown!r}"))
fails += not ok

# the three real roadmaps (this repo, and the sibling worktrees / clones when present) pass
repo = os.path.dirname(os.path.dirname(HERE))
for r in [repo] + [os.environ[k] for k in ("MP_REPO", "CM_REPO") if os.environ.get(k)]:
    p = subprocess.run([sys.executable, "-I", TOOL, "roadmap", "--file", os.path.join(r, "CHANGELOG.md"),
                        "--roadmap", os.path.join(r, "ROADMAP.md")], capture_output=True, text=True)
    ok = p.returncode == 0
    print(("PASS " if ok else "FAIL ") + f"real roadmap {os.path.basename(r)}" + ("" if ok else f": {p.stdout.strip()}"))
    fails += not ok

print(f"ROADMAP TEST {'PASS' if not fails else 'FAIL'} ({fails} failed)")
sys.exit(1 if fails else 0)
