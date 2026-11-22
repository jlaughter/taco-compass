#!/usr/bin/env python3
"""Mimic the Arduino IDE's .ino preprocessing closely enough to catch its classic traps:
it prepends #include <Arduino.h> and inserts a prototype for every top-level function
just above the FIRST function definition, so any type a signature uses must already
be declared by then (i.e. come from a header, not be defined later in the sketch)."""
import re, sys

src = open(sys.argv[1]).read()
lines = src.split("\n")
# A top-level function definition: starts at column 0, has a parameter list, opens a brace.
fn = re.compile(r"^(?!(?:if|for|while|switch|return|else|namespace|struct|class|enum)\b)"
                r"((?:static\s+|inline\s+)*[A-Za-z_][\w:<>,\s\*&]*?[\s\*&])([A-Za-z_]\w*)\s*\(([^;{}]*)\)\s*(?:const\s*)?\{")
protos, first = [], None
depth = 0
for i, ln in enumerate(lines):
    if depth == 0:
        m = fn.match(ln)
        if m and m.group(2) not in ("setup", "loop") or (m and first is None):
            if first is None:
                first = i
            protos.append(f"{m.group(1).strip()} {m.group(2)}({m.group(3).strip()});")
    depth += ln.count("{") - ln.count("}")
out = ["#include <Arduino.h>"] + lines[:first] + ["// --- auto prototypes (as the Arduino IDE would insert) ---"] + protos + lines[first:]
open(sys.argv[2], "w").write("\n".join(out))
