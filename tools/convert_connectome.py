#!/usr/bin/env python3
"""
convert_connectome.py — build the C. elegans connectome header for the ESP32.

Reads the Cook et al. 2019 full hermaphrodite connectome (as packaged by the
OpenWorm c302 project) plus OpenWorm's neuron/muscle metadata, and emits a
compact C header (`include/connectome.h`) containing:

  - the full node table (302 neurons + 95 body-wall muscles + pharyngeal,
    vulval, uterine, enteric muscles), with name, class, function flags,
    neurotransmitter and chemical-synapse sign (+1 excitatory / -1 inhibitory)
  - the chemical synapse edge list (source, target, raw count)
  - the gap junction (electrical) edge list

Provenance:
  - edges:   c302/data/herm_full_edgelist_MODIFIED.csv  (Cook et al. 2019)
             https://github.com/openworm/c302
  - nodes:   c302/data/owmeta_cache.json (OpenWorm sci/bio model database)
  - name normalisation follows c302's own reader
    (c302/UpdatedSpreadsheetDataReader2.py): VB01 -> VB1,
    dBWML01 -> MDL01, etc.

Synapse sign: derived from the *presynaptic* neuron's primary neurotransmitter
from owmeta: GABAergic neurons are inhibitory, all others excitatory. Neurons
with no recorded transmitter are treated as excitatory (matches c302's
assumption). This is a stylised model, not electrophysiology.

Usage:
    python3 convert_connectome.py            # regenerates include/connectome.h
    python3 convert_connectome.py --keep-raw # keep downloaded raw files
"""

import argparse
import csv
import json
import re
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RAW = ROOT / "data" / "raw"
INC = ROOT / "include"

BASE = "https://raw.githubusercontent.com/openworm/c302/master/c302/data/"
FILES = {
    "edgelist": "herm_full_edgelist_MODIFIED.csv",
    "owmeta": "owmeta_cache.json",
}

# --- node classes -----------------------------------------------------------
CLS_NEURON = 0        # 302 neurons
CLS_BODY = 1          # 95 body wall muscles (locomotion)
CLS_PHAR = 2          # pharyngeal muscles pm*/mc* (feeding / pumping)
CLS_VULVAL = 3        # vm* (egg laying)
CLS_UTERINE = 4       # um*
CLS_OTHER = 5         # anal depressor, anal sphincter

# cells that are not worth simulating (non-neural tissue with direct
# innervation in the connectome) — dropped from the node table.
DROPPED = {"hyp", "intL", "intR", "intestine"}

# functional flags (bitmask) taken from owmeta neuron_info type field
F_SENSORY = 1
F_INTER = 2
F_MOTOR = 4

# neurotransmitters
NT_NONE = 0
NT_ACH = 1
NT_GLU = 2
NT_GABA = 3
NT_DA = 4
NT_5HT = 5
NT_OCT = 6
NT_TYR = 7

NT_MAP = {
    "Acetylcholine": NT_ACH,
    "Glutamate": NT_GLU,
    "GABA": NT_GABA,
    "Dopamine": NT_DA,
    "Serotonin": NT_5HT,
    "Octopamine": NT_OCT,
    "Tyramine": NT_TYR,
}

MUSCLE_PREFIX_CLASS = [
    ("pm", CLS_PHAR), ("mc", CLS_PHAR),
    ("vm", CLS_VULVAL), ("um", CLS_UTERINE),
    ("anal", CLS_OTHER), ("sph", CLS_OTHER),
]


def norm_name(name: str) -> str:
    """c302-style normalisation: VB01 -> VB1, dBWML01 -> MDL01."""
    if name[0].isupper() and len(name) >= 2 and name[-2:].startswith("0"):
        return name[:-2] + name[-1:]
    m = re.match(r"^(vBWML|vBWMR|dBWML|dBWMR)(\d+)$", name)
    if m:
        pref = {"vBWML": "MVL", "vBWMR": "MVR", "dBWML": "MDL", "dBWMR": "MDR"}[m.group(1)]
        return pref + m.group(2).zfill(2)
    return name


def muscle_class(name: str) -> int:
    for pref, cls in MUSCLE_PREFIX_CLASS:
        if name.startswith(pref):
            return cls
    return -1


def fetch(url: str, dest: Path) -> None:
    if dest.exists() and dest.stat().st_size > 0:
        print(f"  cached: {dest.name}")
        return
    print(f"  downloading {url}")
    with urllib.request.urlopen(url, timeout=60) as r, open(dest, "wb") as f:
        f.write(r.read())


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep-raw", action="store_true", help="keep raw files after conversion")
    args = ap.parse_args()

    RAW.mkdir(parents=True, exist_ok=True)
    INC.mkdir(parents=True, exist_ok=True)

    print("== fetching raw data ==")
    for key, fn in FILES.items():
        fetch(BASE + fn, RAW / fn)

    print("== parsing ==")
    rows = list(csv.DictReader(open(RAW / FILES["edgelist"])))
    ow = json.load(open(RAW / FILES["owmeta"]))
    ni, mi = ow["neuron_info"], ow["muscle_info"]

    # ---- node table -------------------------------------------------------
    nodes = {}          # norm_name -> index
    meta = []           # parallel list: [name, cls, func, sign, nt]
    neuron_count = 0

    def add_node(name: str, cls: int, func: int, sign: int, nt: int) -> None:
        if name in nodes:
            return
        nodes[name] = len(meta)
        meta.append([name, cls, func, sign, nt])

    # neurons first (sorted, deterministic), from owmeta metadata
    for name in sorted(ni.keys()):
        info = ni[name]
        ftypes = info[1] if len(info) > 1 else []
        func = 0
        for ft in ftypes:
            if "sensory" in ft:
                func |= F_SENSORY
            if "interneuron" in ft:
                func |= F_INTER
            if "motor" in ft:
                func |= F_MOTOR
        nts = info[3] if len(info) > 3 else []
        nt = NT_MAP.get(nts[0], NT_NONE) if nts else NT_NONE
        sign = -1 if nt == NT_GABA else +1
        add_node(name, CLS_NEURON, func, sign, nt)
        neuron_count += 1

    # body wall muscles from owmeta muscle_info (MDL/MDR/MVL/MVR)
    for name in sorted(mi.keys()):
        add_node(name, CLS_BODY, 0, +1, NT_NONE)

    # remaining non-neuron nodes from the edgelist (pharyngeal, vulval, ...)
    for r in rows:
        for fld in ("Source", "Target"):
            n = norm_name(r[fld].strip())
            if n in nodes or n in DROPPED:
                continue
            cls = muscle_class(n)
            if cls < 0:
                print(f"  !!! unclassified node, skipping: {n}")
                continue
            add_node(n, cls, 0, +1, NT_NONE)

    # ---- edges ------------------------------------------------------------
    chem = []
    gap = []
    skipped = 0
    for r in rows:
        s = norm_name(r["Source"].strip())
        t = norm_name(r["Target"].strip())
        if s in DROPPED or t in DROPPED:
            skipped += 1
            continue
        si, ti = nodes.get(s), nodes.get(t)
        if si is None or ti is None:
            bad = s if si is None else t
            print(f"  !!! edge endpoint not in table ({bad}): {s} -> {t}")
            skipped += 1
            continue
        w = int(r["Weight"].strip())
        if r["Type"].strip() == "chemical":
            chem.append((si, ti, w))
        else:
            gap.append((si, ti, w))

    print(f"  nodes: {len(meta)}  (neurons {neuron_count})")
    print(f"  edges: chemical {len(chem)}, gap {len(gap)}, skipped {skipped}")

    # ---- emit header ------------------------------------------------------
    out = INC / "connectome.h"
    print(f"== writing {out} ==")

    def cls_define(cls: int) -> str:
        return ["NODE_NEURON", "NODE_BODY_MUSCLE", "NODE_PHAR_MUSCLE",
                "NODE_VULVAL_MUSCLE", "NODE_UTERINE_MUSCLE",
                "NODE_OTHER"][cls]

    lines = []
    lines.append("// AUTO-GENERATED by tools/convert_connectome.py — do not edit")
    lines.append("// C. elegans full hermaphrodite connectome, Cook et al. 2019")
    lines.append("// (via OpenWorm c302: herm_full_edgelist_MODIFIED.csv + owmeta_cache.json)")
    lines.append("#ifndef CONNECTOME_H")
    lines.append("#define CONNECTOME_H")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append(f"#define N_NEURONS       {neuron_count}")
    lines.append(f"#define N_NODES         {len(meta)}")
    lines.append(f"#define N_EDGES_CHEM    {len(chem)}")
    lines.append(f"#define N_EDGES_GAP     {len(gap)}")
    lines.append("")
    lines.append("enum { NODE_NEURON=0, NODE_BODY_MUSCLE, NODE_PHAR_MUSCLE,")
    lines.append("       NODE_VULVAL_MUSCLE, NODE_UTERINE_MUSCLE, NODE_OTHER };")
    lines.append("enum { F_SENSORY=1, F_INTER=2, F_MOTOR=4 };")
    lines.append("enum { NT_NONE=0, NT_ACH, NT_GLU, NT_GABA, NT_DA, NT_5HT, NT_OCT, NT_TYR };")
    lines.append("")
    lines.append("typedef struct {")
    lines.append("    const char* name;   // canonical name e.g. \"ASEL\"")
    lines.append("    uint8_t cls;        // NODE_*")
    lines.append("    uint8_t func;       // F_* bitmask (neurons)")
    lines.append("    int8_t  sign;       // +1 / -1 chemical output sign")
    lines.append("    uint8_t nt;         // NT_* neurotransmitter")
    lines.append("} NodeMeta;")
    lines.append("")
    lines.append("typedef struct { uint16_t src, dst; uint8_t w; } SynEdge;")
    lines.append("")
    lines.append("extern const NodeMeta node_meta[N_NODES];")
    lines.append("extern const SynEdge chem_edges[N_EDGES_CHEM];")
    lines.append("extern const SynEdge gap_edges[N_EDGES_GAP];")
    lines.append("")
    lines.append("int node_index(const char* name);")
    lines.append("")
    lines.append("#endif // CONNECTOME_H")
    out.write_text("\n".join(lines) + "\n")

    out = INC / "connectome_data.c"
    print(f"== writing {out} ==")
    lines = []
    lines.append("// AUTO-GENERATED by tools/convert_connectome.py — do not edit")
    lines.append('#include "connectome.h"')
    lines.append("")
    lines.append("const NodeMeta node_meta[N_NODES] = {")
    for name, cls, func, sign, nt in meta:
        lines.append(f'    {{"{name}", {cls}, {func}, {sign}, {nt}}},')
    lines.append("};")
    lines.append("")
    lines.append("const SynEdge chem_edges[N_EDGES_CHEM] = {")
    for s, t, w in chem:
        lines.append(f"    {{{s}, {t}, {w}}},")
    lines.append("};")
    lines.append("")
    lines.append("const SynEdge gap_edges[N_EDGES_GAP] = {")
    for s, t, w in gap:
        lines.append(f"    {{{s}, {t}, {w}}},")
    lines.append("};")
    lines.append("")
    out.write_text("\n".join(lines) + "\n")

    if not args.keep_raw:
        import shutil
        for fn in FILES.values():
            (RAW / fn).unlink(missing_ok=True)
        RAW.rmdir() if not list(RAW.iterdir()) else None

    print("== done ==")


if __name__ == "__main__":
    sys.exit(main())
