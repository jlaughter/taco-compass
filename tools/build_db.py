#!/usr/bin/env python3
"""
Build TacoCompass/places_db.h: the list of places the compass points to.

One category per build. Two ways in:

  1) Your own list as KML or KMZ (e.g. exported from Google My Maps):
       python3 tools/build_db.py breweries.kmz
       python3 tools/build_db.py spots.kml --title "Breweries" --device-name "Brew Compass" --icon mug.png

  2) The built-in Taco Bell preset (the default with no file), from All the Places
     (Taco Bell's own store list, full addresses):
       python3 tools/build_db.py
       python3 tools/build_db.py --source overpass # OpenStreetMap instead

KML elements recognized (deliberately few):
  Document/name                     -> title (unless --title)
  Placemark/name                    -> place name
  Placemark/address                 -> "street, city, ST ZIP" (split at the first comma)
  Placemark/description             -> used for the address if <address> is missing (first line)
  Placemark/Point/coordinates       -> lon,lat[,alt]   (KML order: longitude first!)
  KMZ: the first .kml inside; the first PNG/JPG inside becomes the icon unless --icon is given.
Everything else (folders are walked through, styles, lines, polygons, ExtendedData...) is ignored.

Icon: any PNG/JPG, pre-scaled to 96 px and 28 px boxes, transparency kept as a 1-bit mask. Needs Pillow only if
you use an icon. Everything else is standard library.
"""
import argparse, html, io, json, math, os, re, sys, time, unicodedata, urllib.parse, urllib.request, zipfile
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "TacoCompass", "places_db.h")
UA = "TacoCompass-db-builder/2.0 (personal hobby project)"
OVERPASS = "https://overpass-api.de/api/interpreter"
OVERPASS_TB = '[out:json][timeout:240];nwr["brand:wikidata"="Q752941"];out center tags;'
ATP_LATEST = "https://data.alltheplaces.xyz/runs/latest.json"
THEMES = {"taco": 0, "pin": 1, "icon": 2}
ICON_SIZES = {"L": 96, "S": 28}  # splash/arrival, and the slot row

STATE_ABBR = {
    "alabama": "AL", "alaska": "AK", "arizona": "AZ", "arkansas": "AR", "california": "CA",
    "colorado": "CO", "connecticut": "CT", "delaware": "DE", "florida": "FL", "georgia": "GA",
    "hawaii": "HI", "idaho": "ID", "illinois": "IL", "indiana": "IN", "iowa": "IA", "kansas": "KS",
    "kentucky": "KY", "louisiana": "LA", "maine": "ME", "maryland": "MD", "massachusetts": "MA",
    "michigan": "MI", "minnesota": "MN", "mississippi": "MS", "missouri": "MO", "montana": "MT",
    "nebraska": "NE", "nevada": "NV", "new hampshire": "NH", "new jersey": "NJ", "new mexico": "NM",
    "new york": "NY", "north carolina": "NC", "north dakota": "ND", "ohio": "OH", "oklahoma": "OK",
    "oregon": "OR", "pennsylvania": "PA", "rhode island": "RI", "south carolina": "SC",
    "south dakota": "SD", "tennessee": "TN", "texas": "TX", "utah": "UT", "vermont": "VT",
    "virginia": "VA", "washington": "WA", "west virginia": "WV", "wisconsin": "WI", "wyoming": "WY",
    "district of columbia": "DC",
}


def http_get(url, data=None, headers=None, timeout=300):
    h = {"User-Agent": UA}
    h.update(headers or {})
    with urllib.request.urlopen(urllib.request.Request(url, data=data, headers=h), timeout=timeout) as r:
        return r.read()


def place(lat, lon, name="", street="", city=""):
    return {"lat": float(lat), "lon": float(lon), "name": name, "street": street, "city": city}


# ---------------------------------------------------------------- KML / KMZ
def local(tag):
    return tag.rsplit("}", 1)[-1]


def child(el, name):
    for c in el:
        if local(c.tag) == name:
            return c
    return None


def child_text(el, name):
    c = child(el, name)
    return (c.text or "").strip() if c is not None else ""


def strip_html(s):
    s = re.sub(r"(?i)<br\s*/?>|</p>|</div>", "\n", s)
    s = re.sub(r"<[^>]+>", "", s)
    return html.unescape(s)


def split_address(addr):
    """'123 Main St, Atlanta, GA 30318, USA' -> ('123 Main St', 'Atlanta, GA')."""
    parts = [p.strip() for p in addr.replace("\n", ",").split(",") if p.strip()]
    if parts and parts[-1].lower() in ("usa", "us", "united states", "united states of america"):
        parts.pop()
    if not parts:
        return "", ""
    street, rest = parts[0], parts[1:]
    if rest:
        rest[-1] = re.sub(r"\s+\d{5}(-\d{4})?$", "", rest[-1])  # drop ZIP
        m = re.fullmatch(r"([A-Za-z .]+?)\s*$", rest[-1])
        if m and m.group(1).lower() in STATE_ABBR:
            rest[-1] = STATE_ABBR[m.group(1).lower()]
    return street, ", ".join(p for p in rest if p)


def parse_kml(data):
    root = ET.fromstring(data)
    doc_name = ""
    for el in root.iter():
        if local(el.tag) == "Document":
            doc_name = child_text(el, "name")
            break
    out, skipped = [], 0
    for pm in root.iter():
        if local(pm.tag) != "Placemark":
            continue
        pt = child(pm, "Point")
        coords = child_text(pt, "coordinates") if pt is not None else ""
        try:
            lon, lat = [float(v) for v in coords.split()[0].split(",")[:2]]
        except (ValueError, IndexError):
            skipped += 1  # not a point (line/polygon) or malformed
            continue
        addr = child_text(pm, "address")
        if not addr:
            desc = strip_html(child_text(pm, "description"))
            addr = next((ln.strip() for ln in desc.splitlines() if ln.strip()), "")
        street, city = split_address(addr)
        out.append(place(lat, lon, child_text(pm, "name"), street, city))
    if skipped:
        print(f"  skipped {skipped} placemarks without a usable Point", file=sys.stderr)
    return doc_name, out


def load_kml_file(path):
    """Returns (doc_name, places, icon_bytes_or_None)."""
    with open(path, "rb") as f:
        raw = f.read()
    if raw[:2] != b"PK":  # plain KML
        name, places = parse_kml(raw)
        return name, places, None
    zf = zipfile.ZipFile(io.BytesIO(raw))
    kmls = sorted((n for n in zf.namelist() if n.lower().endswith(".kml")),
                  key=lambda n: (n.lower() != "doc.kml", n))
    if not kmls:
        sys.exit(f"{path}: no .kml inside the KMZ")
    name, places = parse_kml(zf.read(kmls[0]))
    imgs = sorted(n for n in zf.namelist() if n.lower().endswith((".png", ".jpg", ".jpeg")))
    return name, places, zf.read(imgs[0]) if imgs else None


# ---------------------------------------------------------------- Taco Bell preset
def from_tags(lat, lon, p):
    street = p.get("addr:street_address") or " ".join(
        x for x in (p.get("addr:housenumber"), p.get("addr:street")) if x)
    state = p.get("addr:state", "")
    state = STATE_ABBR.get(state.strip().lower(), state)
    city = ", ".join(x for x in (p.get("addr:city", ""), state) if x)
    return place(lat, lon, "", street, city)  # every name is "Taco Bell": leave it blank


def fetch_overpass():
    print("Querying Overpass (this can take a minute)...", file=sys.stderr)
    obj = json.loads(http_get(OVERPASS, data=urllib.parse.urlencode({"data": OVERPASS_TB}).encode()))
    out = []
    for e in obj.get("elements", []):
        c = e if "lat" in e else e.get("center")
        if c:
            out.append(from_tags(c["lat"], c["lon"], e.get("tags", {})))
    return out


def load_geojson(obj):
    out = []
    for f in obj.get("features", []):
        g = f.get("geometry") or {}
        if g.get("type") == "Point":
            lon, lat = g["coordinates"][:2]
            out.append(from_tags(lat, lon, f.get("properties", {})))
    return out


class HttpRangeFile(io.RawIOBase):
    """Seekable read-only file over HTTP range requests, enough for zipfile.

    If the server ignores Range (answers 200 with the whole body), falls back to
    downloading the file once to a temporary file and reading from that.
    """

    def __init__(self, url):
        self.url, self.pos, self.fetched, self.local = url, 0, 0, None
        req = urllib.request.Request(url, method="HEAD", headers={"User-Agent": UA})
        with urllib.request.urlopen(req, timeout=60) as r:
            self.url = r.geturl()  # follow redirects once
            self.size = int(r.headers["Content-Length"])

    def seekable(self): return True
    def readable(self): return True
    def tell(self): return self.pos

    def seek(self, off, whence=0):
        self.pos = {0: off, 1: self.pos + off, 2: self.size + off}[whence]
        return self.pos

    def _download_all(self, first_response):
        import tempfile
        print(f"  server ignored range requests; downloading all {self.size / 1e9:.2f} GB once...",
              file=sys.stderr)
        self.local = tempfile.TemporaryFile()
        done, last = 0, 0
        while True:
            chunk = first_response.read(1 << 20)
            if not chunk:
                break
            self.local.write(chunk)
            done += len(chunk)
            if done - last >= 50e6:
                last = done
                print(f"    {done / 1e6:.0f} MB", file=sys.stderr)
        self.fetched = done

    def readinto(self, b):
        mv = memoryview(b).cast("B")
        n = len(mv)
        if self.pos >= self.size or n == 0:
            return 0
        if self.local is None:
            end = min(self.pos + n, self.size) - 1
            req = urllib.request.Request(self.url, headers={"User-Agent": UA,
                                                            "Range": f"bytes={self.pos}-{end}"})
            with urllib.request.urlopen(req, timeout=300) as r:
                if r.status == 206:
                    data = r.read(end - self.pos + 1)
                    mv[:len(data)] = data
                    self.pos += len(data)
                    self.fetched += len(data)
                    return len(data)
                self._download_all(r)
        self.local.seek(self.pos)
        got = self.local.readinto(mv)
        self.pos += got
        return got


def fetch_atp():
    meta = json.loads(http_get(ATP_LATEST))
    rf = HttpRangeFile(meta["output_url"])
    zf = zipfile.ZipFile(io.BufferedReader(rf, buffer_size=1 << 20))
    out = []
    for n in zf.namelist():
        if os.path.basename(n).startswith("taco_bell") and n.endswith(".geojson"):
            data = zf.read(n)
            if data.strip():
                out += load_geojson(json.loads(data))
    print(f"  transferred {rf.fetched / 1e6:.1f} MB of {rf.size / 1e9:.2f} GB", file=sys.stderr)
    return out


# ---------------------------------------------------------------- processing
def dist_m(a, b):
    p1, p2 = math.radians(a["lat"]), math.radians(b["lat"])
    dp, dl = p2 - p1, math.radians(b["lon"] - a["lon"])
    h = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * 6371008.8 * math.asin(math.sqrt(h))


def dedupe(rows, radius_m=30.0):
    """Drop same-named near-duplicates (node+way of one store, repeated pins)."""
    cell, keep = {}, []
    for r in rows:
        k = (round(r["lat"] * 200), round(r["lon"] * 200))
        dup = None
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                for o in cell.get((k[0] + dy, k[1] + dx), []):
                    if o["name"].lower() == r["name"].lower() and dist_m(r, o) < radius_m:
                        dup = o
        if dup is not None:
            if len(r["street"]) > len(dup["street"]):
                dup.update(street=r["street"], city=r["city"])
            continue
        cell.setdefault(k, []).append(r)
        keep.append(r)
    return keep


def clean(s, n):
    s = str(s)
    for a, b in (("’", "'"), ("‘", "'"), ("´", "'"), ("`", "'"), ("–", "-"), ("—", "-"),
                 ("ß", "ss"), ("Æ", "AE"), ("æ", "ae"), ("Ø", "O"), ("ø", "o"), ("Ł", "L"), ("ł", "l")):
        s = s.replace(a, b)
    # The fonts are ASCII-only: strip accents (Alcalá -> Alcala) rather than whole letters.
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode()
    s = " ".join(s.split()).replace("|", "/").replace("\\", "/")
    return s if len(s) <= n else s[: n - 1].rstrip() + "."


def c_str(s):
    return '"' + s.replace('"', '\\"').replace("?", "\\?") + '"'


def icon_arrays(img_bytes, box):
    from PIL import Image  # only needed with an icon
    im = Image.open(io.BytesIO(img_bytes)).convert("RGBA")
    bbox = im.getchannel("A").point(lambda a: 255 if a >= 128 else 0).getbbox()
    if bbox:
        im = im.crop(bbox)  # trim transparent margins so the art fills the slot
    im.thumbnail((box, box), Image.LANCZOS)
    w, h = im.size
    raw = im.tobytes()
    pix, mask = [], bytearray((w * h + 7) // 8)
    for i in range(w * h):
        r, g, b, a = raw[4 * i:4 * i + 4]
        pix.append(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
        if a >= 128:
            mask[i >> 3] |= 0x80 >> (i & 7)
    return w, h, pix, mask


def write_icon(f, tag, icon):
    w, h, pix, mask = icon if icon else (1, 1, [0], b"\0")
    f.write(f"#define DB_ICON_{tag}_W {w}\n#define DB_ICON_{tag}_H {h}\n")
    f.write(f"static const uint16_t DB_ICON_{tag}_PIX[] = {{\n")
    for i in range(0, len(pix), 16):
        f.write("  " + ",".join(f"0x{v:04X}" for v in pix[i:i + 16]) + ",\n")
    f.write(f"}};\nstatic const uint8_t DB_ICON_{tag}_MASK[] = {{\n")
    for i in range(0, len(mask), 24):
        f.write("  " + ",".join(f"0x{v:02X}" for v in mask[i:i + 24]) + ",\n")
    f.write("};\n")


def write_header(rows, path, meta, icon):
    rows = sorted(rows, key=lambda r: (r["lat"], r["lon"]))
    title_l = meta["title"].strip().lower()
    labels, locs, ofs = [], [], 0
    for r in rows:
        name = clean(r["name"], 32)
        if name.lower() == title_l:
            name = ""  # don't repeat the category on every entry
        lab = "|".join((name, clean(r["street"], 32), clean(r["city"], 32)))
        locs.append((round(r["lat"] * 1e6), round(r["lon"] * 1e6), ofs))
        labels.append(lab)
        ofs += len(lab) + 1
    n = len(rows)
    with open(path, "w", newline="\n") as f:
        f.write("// GENERATED by tools/build_db.py - do not edit.\n")
        f.write(f"// {n} places. Built {time.strftime('%Y-%m-%d')}. Source: {meta['source']}\n")
        f.write('#pragma once\n#include <stdint.h>\n#include "geo.h"\n\n')
        f.write(f"#define DB_COUNT {n}\n")
        f.write(f"#define DB_BUILD_DATE \"{time.strftime('%Y-%m-%d')}\"\n")
        f.write(f"#define DB_TITLE {c_str(clean(meta['title'], 24))}\n")
        f.write(f"#define DB_DEVICE_NAME {c_str(clean(meta['device_name'], 20))}\n")
        f.write(f"#define DB_THEME {THEMES[meta['theme']]}  // 0 taco, 1 pin, 2 icon\n\n")
        f.write(f"#define DB_HAS_ICON {1 if icon else 0}\n")
        for tag in ICON_SIZES:
            write_icon(f, tag, icon[tag] if icon else None)
        f.write("\n")
        f.write("static const PlaceLoc DB_LOCS[] = {\n")
        if not locs:
            f.write("  {0, 0, 0},  // placeholder; DB_COUNT is 0\n")
        for la, lo, o in locs:
            f.write(f"  {{{la},{lo},{o}}},\n")
        f.write("};\n\n// \"name|street|city, ST\" per place (name empty when it's just the title)\n")
        f.write("static const char DB_LABELS[] =\n")
        if not labels:
            f.write('  "";\n')
        for i, lab in enumerate(labels):
            f.write(f"  {c_str(lab)[:-1]}\\0\"{';' if i == n - 1 else ''}\n")
    size = n * 12 + ofs + (sum(v[0] * v[1] * 2 for v in icon.values()) if icon else 0)
    print(f"Wrote {path}: {n} places, title '{meta['title']}', theme {meta['theme']}"
          f"{', icon %dx%d' % icon['L'][:2] if icon else ''}, ~{size / 1024:.0f} KB of flash", file=sys.stderr)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*", help=".kml / .kmz files (all go into one category)")
    ap.add_argument("--source", choices=["overpass", "atp"], default="atp",
                    help="Taco Bell preset source (default atp: official store addresses)")
    ap.add_argument("--geojson", action="append", default=[], help=argparse.SUPPRESS)  # tests / ATP dumps
    ap.add_argument("--title", help='category, e.g. "Breweries" (default: KML document name)')
    ap.add_argument("--device-name", help='splash name, e.g. "Brew Compass" (default "Taco Compass")')
    ap.add_argument("--theme", choices=THEMES, help="taco, pin, or icon (default: taco for Taco Bell, "
                                                    "icon if an icon is available, else pin)")
    ap.add_argument("--icon", help="PNG/JPG to use as the place icon")
    ap.add_argument("--bbox", help="minLat,minLon,maxLat,maxLon filter")
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--empty", action="store_true", help="write an empty placeholder")
    a = ap.parse_args()

    rows, icon_bytes, doc_title, sources = [], None, "", []
    tacobell = not a.files and not a.geojson and not a.empty
    for p in a.files:
        name, got, img = load_kml_file(p)
        print(f"{p}: {len(got)} places{' + icon' if img else ''}", file=sys.stderr)
        rows += got
        doc_title = doc_title or name
        icon_bytes = icon_bytes or img
        sources.append(os.path.basename(p))
    for p in a.geojson:
        with open(p) as f:
            rows += load_geojson(json.load(f))
        sources.append(os.path.basename(p))
    if tacobell:
        rows += fetch_atp() if a.source == "atp" else fetch_overpass()
        sources.append("All the Places (CC0)" if a.source == "atp" else "OpenStreetMap contributors (ODbL)")
    if a.icon:
        with open(a.icon, "rb") as f:
            icon_bytes = f.read()

    taco_default = tacobell or bool(a.geojson) or (a.empty and not a.files)
    meta = {
        "title": a.title or ("Taco Bell" if taco_default else doc_title or "Places"),
        "device_name": a.device_name or "Taco Compass",
        "theme": a.theme or ("taco" if taco_default and not a.icon else "icon" if icon_bytes else "pin"),
        "source": "; ".join(sources) or "empty placeholder",
    }
    if meta["theme"] == "icon" and not icon_bytes:
        sys.exit("--theme icon needs an icon (--icon file.png, or a KMZ that contains one)")
    icon = {t: icon_arrays(icon_bytes, b) for t, b in ICON_SIZES.items()} if icon_bytes else None

    if a.bbox:
        s, w, n, e = map(float, a.bbox.split(","))
        rows = [r for r in rows if s <= r["lat"] <= n and w <= r["lon"] <= e]
    rows = [r for r in rows if -90 <= r["lat"] <= 90 and -180 <= r["lon"] <= 180
            and not (r["lat"] == 0 and r["lon"] == 0)]
    before = len(rows)
    rows = dedupe(rows)
    if before:
        print(f"{before} places -> {len(rows)} after de-duplication", file=sys.stderr)
    if not rows and not a.empty:
        sys.exit("Nothing to write.")
    write_header(rows, a.out, meta, icon)


if __name__ == "__main__":
    main()
