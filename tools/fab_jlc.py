"""JLCPCB fabrication package: Gerbers + drill (one zip per board), assembly BOM and pick-and-place.

  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 fab_jlc.py [project_dir]

The BOM is built from the BOARD, not the schematic, so it always matches what is actually placed.
Excluded: test points (TP*), mounting holes (H*), anything flagged DNP or "exclude from BOM", any
footprint with no value or a "DNP ..." value (option pads), and any part that resolves to no MPN.
One line per orderable part: identical parts are grouped by MPN + footprint, so the same component
placed under different value annotations ("100k", "100k (HR=1)") collapses onto a single row.

Outputs, per board, in <project_dir>/fab/:
  Gerbers/MDX-1_<board>_gerbers.zip   Gerbers + Excellon drill (plated and non-plated separately)
  BOM/MDX-1_<board>_BOM.csv           Comment, Designator, Footprint, Quantity, Manufacturer, MPN, LCSC Part #
  PNP/MDX-1_<board>_CPL.csv           Designator, Mid X, Mid Y, Layer, Rotation
  <board>/                            the raw Gerber/drill set (also zipped above)
"""
import os, re, csv, json, glob, zipfile, subprocess, sys, collections

HERE = os.path.dirname(os.path.abspath(__file__))
ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
ONLY = [a for a in ARGS if a in ("main", "ui")]                     # optional: regenerate one board only
PRJ = ([a for a in ARGS if a not in ("main", "ui")] or [os.path.join(HERE, "..", "hardware", "mdx1")])[0]
KCLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"
KPY = "/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3"

BOARDS = [("main", "mdx1.kicad_pcb", "F.Cu,In1.Cu,In2.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts"),
          ("ui",   "mdx1_ui.kicad_pcb", "F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts")]

SKIP_PREFIX = ("TP", "H")          # test points and mounting holes are not ordered parts


def run(*a):
    r = subprocess.run(list(a), capture_output=True, text=True)
    if r.returncode: print("   ! %s\n     %s" % (" ".join(a[:4]), (r.stderr or r.stdout).strip()[:300]))
    return r.returncode == 0


def clean_mpn(m):
    """drop obviously malformed part numbers (an old generator bug produced float artefacts)"""
    m = (m or "").strip()
    if not m or m in ("-", "?"): return ""
    if re.search(r"\d e?[-+]\d|e-0\d|\.\d{6,}", m): return ""
    return m


def eia_size(fp):
    """0402 from 'C_0402_1005Metric' - bom_data.json stores the short EIA code as the package,
    the board stores the full footprint name, so the two only join through this."""
    m = re.search(r"_(\d{4})_\d{4}Metric", fp) or re.search(r"\b(\d{4})\b", fp)
    return m.group(1) if m else ""


def norm(v):
    return re.sub(r"\s+", " ", (v or "").strip().lower())


def strip_paren(v):
    """'100k (FSMODE=0 slave)' -> '100k' : the bracketed note is a per-instance annotation,
    not part of the orderable value, so identical parts read the same once it is removed."""
    return re.sub(r"\s*\([^)]*\)", "", v or "").strip()


def mpn_table():
    """manufacturer / MPN / LCSC, keyed by reference and also by (value, package), from the priced BOM"""
    p = os.path.join(HERE, "bom_data.json")
    by_ref, by_val = {}, {}
    if not os.path.exists(p): return by_ref, by_val
    for row in json.load(open(p)).get("rows", []):
        rec = {"mfr": row.get("Manufacturer", ""), "mpn": clean_mpn(row.get("MPN", "")),
               "lcsc": row.get("LCSC", ""), "desc": row.get("Description", "")}
        if not rec["mpn"] and not rec["mfr"]: continue
        for r in str(row.get("References", "")).replace(" ", "").split(","):
            if r: by_ref[r] = rec
        key = (norm(row.get("Value", "")), norm(row.get("Package", "")))
        by_val.setdefault(key, rec)
        by_val.setdefault((norm(row.get("Value", "")), ""), rec)      # value-only fallback
    return by_ref, by_val


def collect(board_path):
    """value/footprint/attributes of every placed part, via a helper in KiCad's interpreter"""
    script = r'''
import pcbnew, json, sys
b = pcbnew.LoadBoard(sys.argv[1])
out = []
for f in b.GetFootprints():
    a = f.GetAttributes()
    out.append({"ref": f.GetReference(), "val": f.GetValue(), "fp": f.GetFPIDAsString(),
                "dnp": bool(f.IsDNP()), "nobom": bool(a & pcbnew.FP_EXCLUDE_FROM_BOM),
                "smd": bool(a & pcbnew.FP_SMD), "tht": bool(a & pcbnew.FP_THROUGH_HOLE)})
json.dump(out, open(sys.argv[2], "w"))
'''
    tmp_py, tmp_json = "/tmp/_fp_dump.py", "/tmp/_fp_dump.json"
    open(tmp_py, "w").write(script)
    subprocess.run([KPY, tmp_py, board_path, tmp_json], capture_output=True, text=True)
    return json.load(open(tmp_json))


def wanted(fp):
    if fp["nobom"] or fp["dnp"]: return False
    ref = fp["ref"].strip()
    if re.match(r"^(%s)\d*\b" % "|".join(SKIP_PREFIX), ref, re.I): return False
    v = (fp["val"] or "").strip()
    if not v or v.upper() in ("DNP", "NP", "~"): return False
    if re.match(r"^\s*DNP\b", v, re.I): return False    # "DNP (match, shunt)" etc. are option pads, not ordered
    return True


def refkey(r):
    m = re.match(r"([^\d]*)(\d*)", r)
    return (m.group(1), int(m.group(2) or 0))


def main():
    fab = os.path.join(PRJ, "fab"); os.makedirs(fab, exist_ok=True)
    gdir = os.path.join(fab, "Gerbers"); bdir = os.path.join(fab, "BOM"); pdir = os.path.join(fab, "PNP")
    for d in (gdir, bdir, pdir): os.makedirs(d, exist_ok=True)
    _, by_val = mpn_table()
    for name, pcb, layers in BOARDS:
        if ONLY and name not in ONLY: continue
        src = os.path.join(PRJ, pcb)
        if not os.path.exists(src): print("%s: no board file, skipped" % name); continue
        print("\n=== %s board (%s)" % (name, pcb))
        work = os.path.join(fab, name); os.makedirs(work, exist_ok=True)
        for f in glob.glob(os.path.join(work, "*")): os.remove(f)

        # ---- gerbers + drill, zipped
        run(KCLI, "pcb", "export", "gerbers", "--layers", layers, "--no-x2", "--no-netlist",
            "--subtract-soldermask", "--use-drill-file-origin", "-o", work + "/", src)
        run(KCLI, "pcb", "export", "drill", "--format", "excellon", "--excellon-units", "mm",
            "--excellon-separate-th", "--generate-map", "--map-format", "gerberx2",
            "--drill-origin", "absolute", "-o", work + "/", src)
        zpath = os.path.join(gdir, "MDX-1_%s_gerbers.zip" % name)
        files = sorted(glob.glob(os.path.join(work, "*")))
        with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
            for f in files: z.write(f, os.path.basename(f))
        print("   gerbers: %d files -> %s (%.0f KB)" % (len(files), os.path.basename(zpath), os.path.getsize(zpath) / 1024))

        # ---- parts from the board
        parts = collect(src)
        keep = [p for p in parts if wanted(p)]
        dropped = collections.Counter()
        for p in parts:
            if p in keep: continue
            if p["nobom"] or p["dnp"]: dropped["marked DNP / not in BOM"] += 1
            elif re.match(r"^TP", p["ref"], re.I): dropped["test points"] += 1
            elif re.match(r"^H\d", p["ref"], re.I): dropped["mounting holes"] += 1
            else: dropped["no value"] += 1

        # resolve every kept part to its priced record, matching on value+package (never on
        # reference: references are renumbered whenever the schematic is regenerated, and a stale
        # reference silently returns the WRONG part).
        groups = {}
        for p in keep:
            pkg = p["fp"].split(":")[-1]
            m = (by_val.get((norm(p["val"]), norm(pkg)))
                 or by_val.get((norm(p["val"]), eia_size(pkg)))
                 or by_val.get((norm(p["val"]), "")))
            mpn = ((m or {}).get("mpn") or "").strip()
            if not mpn or mpn.upper() == "DNP":       # unresolved or do-not-populate: not an ordered line
                dropped["no MPN / DNP"] += 1
                continue
            # one line per orderable part: identical MPN + footprint, whatever the per-instance
            # value annotation ("100k", "100k (HR=1)" and "100k (slave)" are one 100 k resistor).
            key = (mpn, p["fp"])
            g = groups.setdefault(key, {"refs": [], "vals": [], "mfr": m["mfr"], "mpn": mpn,
                                        "lcsc": m.get("lcsc", ""), "desc": m.get("desc", "")})
            g["refs"].append(p["ref"]); g["vals"].append(p["val"])

        def comment(g):
            bases = sorted({strip_paren(v) for v in g["vals"]})
            if len(bases) == 1: return bases[0]           # e.g. all "100k" -> "100k"
            return g["desc"] or max(bases, key=len)       # differing labels -> the datasheet description

        bom = os.path.join(bdir, "MDX-1_%s_BOM.csv" % name)
        with open(bom, "w", newline="") as fh:
            w = csv.writer(fh)
            w.writerow(["Comment", "Designator", "Footprint", "Quantity", "Manufacturer", "MPN", "LCSC Part #"])
            for (mpn, fp), g in sorted(groups.items(), key=lambda kv: (refkey(sorted(kv[1]["refs"], key=refkey)[0]))):
                refs = sorted(g["refs"], key=refkey)
                w.writerow([comment(g), ",".join(refs), fp.split(":")[-1], len(refs), g["mfr"], g["mpn"], g["lcsc"]])
        print("   BOM: %d lines, %d parts (dropped %s) -> %s" %
              (len(groups), sum(len(g["refs"]) for g in groups.values()), dict(dropped), os.path.basename(bom)))

        # ---- pick and place
        pos = os.path.join(work, "pos.csv")
        run(KCLI, "pcb", "export", "pos", "--format", "csv", "--units", "mm", "--side", "both",
            "--exclude-dnp", "-o", pos, src)
        cpl = os.path.join(pdir, "MDX-1_%s_CPL.csv" % name)
        n = 0
        if os.path.exists(pos):
            rows = list(csv.DictReader(open(pos)))
            with open(cpl, "w", newline="") as fh:
                w = csv.writer(fh); w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
                for r in rows:
                    if not wanted({"ref": r["Ref"], "val": r["Val"], "fp": "", "dnp": False, "nobom": False}): continue
                    w.writerow([r["Ref"], r["PosX"], r["PosY"],
                                "Top" if r["Side"].lower().startswith("t") else "Bottom", r["Rot"]])
                    n += 1
            os.remove(pos)
        print("   CPL: %d placements -> %s" % (n, os.path.basename(cpl)))
    print("\nall outputs in %s" % os.path.abspath(fab))


if __name__ == "__main__":
    main()
