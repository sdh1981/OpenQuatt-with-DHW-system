#!/usr/bin/env python3
"""
generate_hp_perf_map.py
~~~~~~~~~~~~~~~~~~~~~~~
Reads tools/hp_performance_data.json and writes
openquatt/includes/hp_perf_map.h (drop-in replacement).

Usage (run from repo root):
    python tools/generate_hp_perf_map.py

Or with explicit paths:
    python tools/generate_hp_perf_map.py \
        --input  tools/hp_performance_data.json \
        --output openquatt/includes/hp_perf_map.h
"""

import argparse
import json
import os
import sys
from datetime import datetime, timezone

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def fmt_val(v):
    """Return a C++ float literal string: NAN for None/null, else 'x.xxf'."""
    if v is None:
        return "NAN"
    return f"{v:.2f}f"


def render_row(values):
    """Render a single innermost level-axis row as a compact C++ initialiser."""
    return "{" + ", ".join(fmt_val(v) for v in values) + "}"


def render_3d_array(name, data, n_amb, n_sup, n_lvl):
    """
    Render a 3-D float array definition.

    data is a list (amb) of lists (sup) of lists (level).
    Items that are dicts with a "_comment" key emit a C++ line comment before
    the opening brace; all other dicts are silently skipped.
    Only list items are treated as data rows.
    """
    lines = []
    lines.append(f"static constexpr float {name}[N_AMB][N_SUP][N_LVL] = {{")
    for ai, amb_block in enumerate(data):
        # Collect comment (if any) and real data rows
        comment = None
        real_rows = []
        for item in amb_block:
            if isinstance(item, dict):
                if "_comment" in item and comment is None:
                    comment = item["_comment"]
            elif isinstance(item, list):
                real_rows.append(item)

        if len(real_rows) != n_sup:
            raise ValueError(
                f"{name}[{ai}]: expected {n_sup} supply rows, "
                f"found {len(real_rows)}"
            )
        if comment:
            lines.append(f"  // {comment}")
        lines.append("  {")
        for si, row in enumerate(real_rows):
            if len(row) != n_lvl:
                raise ValueError(
                    f"{name}[{ai}][{si}]: expected {n_lvl} level values, "
                    f"found {len(row)}"
                )
            comma = "," if si < n_sup - 1 else ""
            lines.append(f"    {render_row(row)}{comma}")
        amb_comma = "," if ai < n_amb - 1 else ""
        lines.append(f"  }}{amb_comma}")
    lines.append("};")
    return lines


# ---------------------------------------------------------------------------
# Static boilerplate that never changes
# ---------------------------------------------------------------------------

INTERP_FUNCTIONS = """\
static inline int find_interval(const float *bp, int n, float x) {
  if (x <= bp[0]) return 0;
  if (x >= bp[n-1]) return n-2;
  for (int i=0;i<n-1;i++) {
    if (x >= bp[i] && x <= bp[i+1]) return i;
  }
  return n-2;
}

static inline float lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

static inline float bilerp(float q11, float q21, float q12, float q22, float tx, float ty) {
  // x: amb, y: sup
  float r1 = lerp(q11, q21, tx);
  float r2 = lerp(q12, q22, tx);
  return lerp(r1, r2, ty);
}

static inline float interp_3d(const float data[N_AMB][N_SUP][N_LVL], int level, float Tamb, float Tsup) {
  if (level <= 0) return 0.0f;           // level 0 => off
  if (level > N_LVL) level = N_LVL;
  const int li = level - 1;

  int ai = find_interval(T_amb_bp, N_AMB, Tamb);
  int si = find_interval(T_sup_bp, N_SUP, Tsup);

  float ax0 = T_amb_bp[ai];
  float ax1 = T_amb_bp[ai+1];
  float sy0 = T_sup_bp[si];
  float sy1 = T_sup_bp[si+1];

  float tx = (ax1 == ax0) ? 0.0f : (Tamb - ax0) / (ax1 - ax0);
  float ty = (sy1 == sy0) ? 0.0f : (Tsup - sy0) / (sy1 - sy0);
  if (tx < 0) tx = 0; if (tx > 1) tx = 1;
  if (ty < 0) ty = 0; if (ty > 1) ty = 1;

  float q11 = data[ai][si][li];
  float q21 = data[ai+1][si][li];
  float q12 = data[ai][si+1][li];
  float q22 = data[ai+1][si+1][li];

  // If any cell is missing (NaN), return NaN so the caller can fallback/skip.
  if (std::isnan(q11) || std::isnan(q21) || std::isnan(q12) || std::isnan(q22)) return NAN;

  return bilerp(q11, q21, q12, q22, tx, ty);
}

static inline float interp_power_th_w(int level, float Tamb, float Tsup) {
  return interp_3d(P_th_W, level, Tamb, Tsup);
}

static inline float interp_cop(int level, float Tamb, float Tsup) {
  return interp_3d(COP, level, Tamb, Tsup);
}

static inline float interp_power_el_w(int level, float Tamb, float Tsup, float cop_fallback=3.0f) {
  float pth = interp_power_th_w(level, Tamb, Tsup);
  if (std::isnan(pth) || pth <= 0.0f) return 0.0f;
  float cop = interp_cop(level, Tamb, Tsup);
  if (std::isnan(cop) || cop <= 0.1f) cop = cop_fallback;
  return pth / cop;
}"""


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def generate(input_path: str, output_path: str) -> None:
    with open(input_path, "r", encoding="utf-8") as f:
        d = json.load(f)

    meta        = d.get("_metadata", {})
    t_amb_bp    = d["T_amb_bp"]
    t_sup_bp    = d["T_sup_bp"]
    n_levels    = d["N_levels"]
    p_th_data   = d["P_th_W"]
    cop_data    = d["COP"]

    n_amb = len(t_amb_bp)
    n_sup = len(t_sup_bp)

    # Sanity checks
    if len(p_th_data) != n_amb:
        raise ValueError(f"P_th_W has {len(p_th_data)} Tamb rows, expected {n_amb}")
    if len(cop_data) != n_amb:
        raise ValueError(f"COP has {len(cop_data)} Tamb rows, expected {n_amb}")

    # Collect estimated rows for header warning comment
    estimated_tamb = []
    for ai, amb_block in enumerate(p_th_data):
        for item in amb_block:
            if isinstance(item, dict) and "ESTIMATED" in item.get("_comment", ""):
                estimated_tamb.append(t_amb_bp[ai])
                break

    # Build source-info comment
    now_utc = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    src_lines = [
        "#pragma once",
        f"// Auto-generated from {os.path.basename(input_path)}",
        f"// Generated by generate_hp_perf_map.py on {now_utc}",
        f"// Model    : {meta.get('model', '?')}",
        f"// Data ver : {meta.get('version', '?')}",
    ]
    if estimated_tamb:
        tamb_str = ", ".join(f"+{t}" if t >= 0 else str(t) for t in estimated_tamb)
        src_lines.append(
            f"// WARNING  : Tamb={tamb_str} rows are ESTIMATED (linear extrapolation)."
        )
        src_lines.append(
            "//            Replace with real manufacturer data when available."
        )
    src_lines.append(
        "// Provides thermal power (W) and COP as function of (level, Tamb, Tsup)."
    )
    src_lines.append("#include <cmath>")
    src_lines.append("")
    src_lines.append("namespace oq_perf {")
    src_lines.append("")

    # Dimensions
    src_lines.append(f"static constexpr int N_AMB = {n_amb};")
    src_lines.append(f"static constexpr int N_SUP = {n_sup};")
    src_lines.append(f"static constexpr int N_LVL = {n_levels}; // levels 1..{n_levels}")
    src_lines.append("")

    # Breakpoint arrays
    amb_vals = ", ".join(f"{v:.2f}f" for v in t_amb_bp)
    sup_vals = ", ".join(f"{v:.2f}f" for v in t_sup_bp)
    src_lines.append(f"static constexpr float T_amb_bp[N_AMB] = {{{amb_vals}}};")
    src_lines.append(f"static constexpr float T_sup_bp[N_SUP] = {{{sup_vals}}};")
    src_lines.append("")

    # P_th_W array
    src_lines += render_3d_array("P_th_W", p_th_data, n_amb, n_sup, n_levels)
    src_lines.append("")

    # COP array
    src_lines += render_3d_array("COP", cop_data, n_amb, n_sup, n_levels)
    src_lines.append("")

    # Interpolation functions (verbatim)
    src_lines.append(INTERP_FUNCTIONS)
    src_lines.append("")
    src_lines.append("} // namespace oq_perf")
    src_lines.append("")

    content = "\n".join(src_lines)

    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    with open(output_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(content)

    print(f"Written: {output_path}")
    print(f"  N_AMB={n_amb}, N_SUP={n_sup}, N_LVL={n_levels}")
    print(f"  T_amb_bp: {t_amb_bp}")
    if estimated_tamb:
        print(f"  ESTIMATED rows: Tamb={estimated_tamb}")


def main():
    # Default paths relative to repo root (where this script is typically invoked)
    default_input  = os.path.join(os.path.dirname(__file__), "hp_performance_data.json")
    default_output = os.path.join(
        os.path.dirname(__file__), "..", "openquatt", "includes", "hp_perf_map.h"
    )

    parser = argparse.ArgumentParser(
        description="Generate hp_perf_map.h from hp_performance_data.json"
    )
    parser.add_argument(
        "--input", "-i",
        default=default_input,
        help="Path to hp_performance_data.json (default: tools/hp_performance_data.json)",
    )
    parser.add_argument(
        "--output", "-o",
        default=default_output,
        help="Path to write hp_perf_map.h (default: openquatt/includes/hp_perf_map.h)",
    )
    args = parser.parse_args()

    input_path  = os.path.normpath(args.input)
    output_path = os.path.normpath(args.output)

    if not os.path.isfile(input_path):
        print(f"ERROR: input file not found: {input_path}", file=sys.stderr)
        sys.exit(1)

    generate(input_path, output_path)


if __name__ == "__main__":
    main()
