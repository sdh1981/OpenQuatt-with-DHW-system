#!/usr/bin/env python3
"""Legt een ODU EEPROM-dump tegen 'MGMT OD Modbus communication protocol.xlsx'.

    python decode.py hp1-eeprom.json [hp2-eeprom.json]

Leest blad 'Address（3000）' voor de EEPROM en de bladen 2100 / 11000 / 11100
voor de identificatieblokken, en zet de gemeten waarden ernaast. Meldt
waarden die buiten het gedocumenteerde bereik vallen -- dat is waar de
werkelijkheid en het document uiteenlopen.

Het protocoldocument zit niet in de repo. Zet het pad in OQ_MODBUS_XLSX, of leg
'MGMT OD Modbus communication protocol.xlsx' naast dit script.

Op Windows: PYTHONIOENCODING=utf-8, anders struikelt de console over de
Chinese tekens in de bladnamen.
"""
import json
import os
import re
import sys

import xlsx

INTERNAL = "internal parameters"

XLSX = os.environ.get(
    "OQ_MODBUS_XLSX",
    os.path.join(os.path.dirname(os.path.abspath(__file__)),
                 "MGMT OD Modbus communication protocol.xlsx"),
)


def load_doc():
    if not os.path.exists(XLSX):
        sys.exit(f"protocoldocument niet gevonden: {XLSX}\n"
                 f"zet het pad in OQ_MODBUS_XLSX")
    z, s = xlsx.open_book(XLSX)
    paths = {name: path for name, path in xlsx.sheet_names(z)}

    def table(sheet_key, addr_col=0, content_col=2, range_col=4):
        path = next(p for n, p in paths.items() if sheet_key in n)
        out = {}
        for row in xlsx.rows(z, path, s):
            if len(row) <= addr_col:
                continue
            a = row[addr_col].strip()
            if not re.fullmatch(r"\d+", a):
                continue
            content = row[content_col].strip() if len(row) > content_col else ""
            rng = row[range_col].strip() if len(row) > range_col else ""
            out[int(a)] = (content, rng)
        return out

    return {
        "eeprom": table("3000"),
        "core": table("2100"),
        "ext": table("11000"),
        "ident": table("11100"),
    }


def parse_range(rng):
    """-> ('span', lo, hi) | ('enum', {waarde: label}) | None"""
    if not rng:
        return None
    text = rng.replace("；", ";").replace("：", ":").replace("，", ",")

    pairs = re.findall(r"(?:^|[\n;,])\s*(\d+)\s*:\s*([^\n;,]+)", text)
    if len(pairs) >= 2:
        return ("enum", {int(k): v.strip() for k, v in pairs})

    m = re.fullmatch(r"\s*(-?\d+)\s*[-~]\s*(-?\d+)\s*", text)
    if m:
        return ("span", int(m.group(1)), int(m.group(2)))
    return None


def check(value, rng):
    """-> (status, toelichting). status: ok / buiten / onbekend"""
    p = parse_range(rng)
    if p is None:
        return "onbekend", ""
    if p[0] == "span":
        _, lo, hi = p
        return ("ok" if lo <= value <= hi else "buiten"), f"bereik {lo}-{hi}"
    labels = p[1]
    if value in labels:
        return "ok", labels[value]
    return "buiten", "gedocumenteerd: " + ", ".join(f"{k}={v}" for k, v in sorted(labels.items()))


def rule(c="-", n=100):
    return c * n


def head(t):
    print()
    print(t)
    print(rule("="))


def report(dump, doc):
    w = {r["sheet_address"]: r["word"] for r in dump["eeprom"]["registers"]}
    ee = doc["eeprom"]
    hp = dump["source"]["hp"]

    head(f"HP{hp}  --  EEPROM 3000-3511 tegen het protocoldocument")

    missing = [a for a in w if a not in ee]
    if missing:
        print(f"  {len(missing)} adressen niet in het document: {missing[:6]}")

    named = out_of_range = internal_nonzero = 0
    print(f"  {'blad':>5}  {'waarde':>6}  omschrijving")
    print("  " + rule("-", 96))
    for a in sorted(w):
        v = w[a]
        content, rng = ee.get(a, ("<niet in document>", ""))
        low = content.lower()

        if low.startswith(INTERNAL):
            if v:
                internal_nonzero += 1
            continue
        if not content:
            continue
        named += 1
        if v == 0 and not rng:
            continue

        status, note = check(v, rng)
        flag = "  "
        if status == "buiten":
            flag = "!!"
            out_of_range += 1
        line = f"  {flag}{a:>4}  {v:>6}  {content}"
        if note:
            line += f"   [{note}]"
        print(line)

    print()
    print(f"  {named} benoemde registers, {internal_nonzero} 'Internal parameters' met inhoud, "
          f"{out_of_range} buiten het gedocumenteerde bereik")


def afwijkingen(dump, doc):
    """Alleen de regels die niet kloppen met het document."""
    w = {r["sheet_address"]: r["word"] for r in dump["eeprom"]["registers"]}
    head(f"HP{dump['source']['hp']}  --  buiten het gedocumenteerde bereik")
    n = 0
    for a in sorted(w):
        content, rng = doc["eeprom"].get(a, ("", ""))
        if not content or content.lower().startswith(INTERNAL):
            continue
        status, note = check(w[a], rng)
        if status == "buiten":
            n += 1
            print(f"  {a}  {content}")
            print(f"       gemeten {w[a]}   {note}")
    if not n:
        print("  geen")


def blocks(dump, doc):
    ident = dump["identity"]
    for key, sheet, label in (
        ("core", "core", "Kernblok (modbus 2114)"),
        ("extended", "ext", "Uitgebreid blok (modbus 11004)"),
    ):
        blk = ident["raw_blocks"][key]
        head(f"{label} -- HP{dump['source']['hp']}")
        start = blk["modbus_start"]
        for i, v in enumerate(blk["values"]):
            # document rekent in bladadressen, modbus = blad - 1
            content, rng = doc[sheet].get(start + i + 1, ("<niet in document>", ""))
            status, note = check(v, rng) if content else ("onbekend", "")
            flag = "!!" if status == "buiten" else "  "
            line = f"  {flag}{start + i:>6}  {v:>6}  {content}"
            if note:
                line += f"   [{note}]"
            print(line)


def compare(a, b, doc):
    wa = {r["sheet_address"]: r["word"] for r in a["eeprom"]["registers"]}
    wb = {r["sheet_address"]: r["word"] for r in b["eeprom"]["registers"]}
    head(f"HP{a['source']['hp']} tegen HP{b['source']['hp']}")
    diff = [x for x in sorted(set(wa) | set(wb)) if wa.get(x) != wb.get(x)]
    if not diff:
        print("  alle 512 registers identiek")
        return
    print(f"  {len(diff)} registers verschillen")
    print()
    print(f"  {'blad':>5}  {'HP' + str(a['source']['hp']):>6}  {'HP' + str(b['source']['hp']):>6}  omschrijving")
    print("  " + rule("-", 96))
    for x in diff:
        content, _ = doc["eeprom"].get(x, ("<niet in document>", ""))
        print(f"  {x:>5}  {str(wa.get(x)):>6}  {str(wb.get(x)):>6}  {content}")


def main():
    if len(sys.argv) not in (2, 3):
        print(__doc__)
        return 1
    doc = load_doc()
    a = json.load(open(sys.argv[1], encoding="utf-8"))

    report(a, doc)
    afwijkingen(a, doc)
    blocks(a, doc)

    if len(sys.argv) == 3:
        b = json.load(open(sys.argv[2], encoding="utf-8"))
        report(b, doc)
        afwijkingen(b, doc)
        compare(a, b, doc)
    return 0


if __name__ == "__main__":
    sys.exit(main())
