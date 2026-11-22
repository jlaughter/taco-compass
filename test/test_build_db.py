#!/usr/bin/env python3
"""Checks tools/build_db.py output for both input paths and that the headers compile."""
import os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "tools", "build_db.py")
SKETCH = os.path.join(HERE, "..", "TacoCompass")
fails = 0


def check(cond, msg):
    global fails
    if not cond:
        fails += 1
        print("FAIL", msg)


def build(*args):
    out = tempfile.mktemp(suffix=".h")
    r = subprocess.run([sys.executable, "-W", "error", TOOL, *args, "--out", out], capture_output=True, text=True)
    check(r.returncode == 0, f"build {args}: {r.stderr}")
    text = open(out).read() if r.returncode == 0 else ""
    if text:  # it must compile as C++
        src = tempfile.mktemp(suffix=".cpp")
        open(src, "w").write(f'#include "{out}"\nint main(){{return DB_COUNT + DB_LABELS[0] + DB_ICON_L_PIX[0];}}\n')
        c = subprocess.run(["g++", "-std=c++17", "-Wall", "-Werror", "-fsyntax-only", f"-I{SKETCH}", src],
                           capture_output=True, text=True)
        check(c.returncode == 0, f"compile {args}: {c.stderr[:500]}")
    return text, r.stderr


def labels(text):
    return re.findall(r'^  "(.*)\\0";?$', text, re.M)


fx = lambda n: os.path.join(HERE, "fixtures", n)

# Taco Bell path (All the Places GeoJSON): names blank, taco theme, no icon
t, _ = build("--geojson", fx("atp_sample.geojson"))
check('#define DB_TITLE "Taco Bell"' in t and "#define DB_THEME 0" in t and "#define DB_HAS_ICON 0" in t, "taco meta")
check(all(l.startswith("|") for l in labels(t)) and len(labels(t)) == 3, f"taco labels {labels(t)}")
check("|1 Fixture A Rd Suite 100|Testville, GA" in labels(t), "state abbreviated + longer address kept")

# Plain KML: pin theme, document name as title, addresses split
t, err = build(fx("breweries.kml"))
L = labels(t)
check('#define DB_TITLE "Breweries"' in t and "#define DB_THEME 1" in t, "kml meta")
check("Mock Ale Works|202 Pretend Pkwy|Mocktown, GA" in L, f"description address {L}")
check("No Address Taproom||" in L, "no address")
check('Quote \\"Test\\" / Pipe Brewing||' in L, f"quote/pipe escaping {L}")
check("skipped 1" in err, "LineString skipped")

# Overrides
t, _ = build(fx("breweries.kml"), "--title", "Taprooms", "--theme", "taco", "--icon", fx("mug.png"))
check('#define DB_TITLE "Taprooms"' in t and "#define DB_THEME 0" in t and "#define DB_HAS_ICON 1" in t, "overrides")
check("|303 Same Name St|Testville, GA" not in labels(t), "name only blanked when equal to title")

# Empty placeholder
t, _ = build("--empty")
check("#define DB_COUNT 0" in t and "#define DB_THEME 0" in t, "empty")

# Bad: icon theme without an icon
r = subprocess.run([sys.executable, TOOL, fx("breweries.kml"), "--theme", "icon", "--out", os.devnull],
                   capture_output=True, text=True)
check(r.returncode != 0, "icon theme without icon must fail")

print("build_db tests passed" if not fails else f"{fails} build_db FAILED")
sys.exit(1 if fails else 0)
