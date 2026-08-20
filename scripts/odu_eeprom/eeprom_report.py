#!/usr/bin/env python3
"""Maakt een ODU EEPROM-dump leesbaar, en vergelijkt er twee.

    python eeprom_report.py hp1-eeprom.json
    python eeprom_report.py hp1-eeprom.json hp2-eeprom.json

Buffer-index 0 is blad 3000 (modbus 2999). De genoemde adressen hieronder
komen uit OpenQuattOduEepromDump.cpp, waar de component ze zelf uitleest.
"""
import json
import sys
from datetime import datetime, timezone

# sheet_address -> label, uit de fingerprints in de component
LABELS = {
    3310: "aantal ventilatoren",
    3317: "model / hoofd-PCB adres",
    3456: "minimum flow",
    3459: "type flowsensor",
    3498: "koudemiddel",
    3502: "pomp/vent vermogen 1",
    3503: "pomp/vent vermogen 2",
    3504: "pomp/vent vermogen 3",
    3505: "pomp/vent vermogen 4",
    3506: "pomp/vent vermogen 5",
    3507: "pomp/vent vermogen 6",
}


def load(path):
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def words(dump):
    """sheet_address -> word, in leesvolgorde."""
    return {r["sheet_address"]: r["word"] for r in dump["eeprom"]["registers"]}


def rule(char="-", width=78):
    return char * width


def head(text):
    print()
    print(text)
    print(rule("="))


def identity(dump):
    ident = dump["identity"]
    ee = dump["eeprom"]
    crc = ee["crc"]
    job = dump["job"]
    src = dump["source"]

    when = datetime.fromtimestamp(dump["captured_at_epoch"], timezone.utc)

    head(f"ODU EEPROM  --  HP{src['hp']}  (modbus adres {src['modbus_device_address']})")
    rows = [
        ("opgenomen", when.strftime("%Y-%m-%d %H:%M:%S UTC")),
        ("duur", f"{job['duration_ms'] / 1000:.1f} s"),
        ("bron", src["snapshot"]),
        ("waarschuwingen", ", ".join(job["warnings"]) if job["warnings"] else "geen"),
    ]
    for k, v in rows:
        print(f"  {k:<24} {v}")

    head("Geldigheid")
    ok = crc["matches_stored_eeprom"]
    print(f"  {'volledig ingelezen':<24} {'ja' if ee['complete'] else 'NEE'}")
    print(f"  {'CRC berekend':<24} {crc['calculated']}")
    print(f"  {'CRC opgeslagen':<24} {crc['stored']}")
    print(f"  {'komt overeen':<24} {'JA' if ok else 'NEE -- dump onbetrouwbaar'}")
    print(f"  {'herleespogingen':<24} {crc['retry_count']}")
    print(f"  {'EEPROM-fout gemeld':<24} {'JA' if ident['eeprom_failure'] else 'nee'}")

    head("Identificatie")
    fw = ident["official_firmware"]
    pcb = ident["pcb_program"]
    rows = [
        ("firmware ODU", f"{fw['label']}  (raw {fw['raw']})"),
        ("PCB-programma", f"{pcb['label']}  main {pcb['main']} sub {pcb['sub']}"),
        ("EEPROM-programma", f"{ident['eeprom_program']['raw']} ({ident['eeprom_program']['hex']})"),
        ("EEPROM-versie uitgebreid", ident["extended_eeprom_version"]),
        ("regelprint artikelnr", f"{ident['control_board_item']['raw']} ({ident['control_board_item']['hex']})"),
        ("projectcode", ident["project_code"]),
        ("hardwareversie", ident["hardware_version"]),
        ("betaversie", ident["beta_version"]),
        ("compressorcode", ident["compressor_code"]),
        ("DIP-schakelaars", f"{ident['odu_dip_switch']} (0x{ident['odu_dip_switch']:04X})"),
        ("ODU-adres", ident["odu_address"]),
        ("uitgebreid blok gelezen", "ja" if ident["extended_supported"] else "nee"),
    ]
    for k, v in rows:
        print(f"  {k:<24} {v}")

    for field, label in (("model", "model"), ("customer_model", "klantmodel"), ("serial", "serienummer")):
        raw = ident["raw_blocks"][field]["values"]
        blank = all(v == 0xFFFF for v in raw)
        text = ident[field] or ("<leeg: alle 20 registers 0xFFFF>" if blank else "<onleesbaar>")
        print(f"  {label:<24} {text}")

    head("Fingerprints")
    for k, v in dump["eeprom"]["fingerprints"].items():
        print(f"  {k:<24} {v}")


def hexdump(w, title="EEPROM 3000-3511, lage byte per register"):
    head(title)
    addrs = sorted(w)
    lo, hi = addrs[0], addrs[-1]
    print("   blad    " + " ".join(f"{i:02X}" for i in range(16)))
    print(rule())
    for base in range(lo, hi + 1, 16):
        cells = []
        for off in range(16):
            v = w.get(base + off)
            cells.append("--" if v is None else f"{v & 0xFF:02X}")
        row = " ".join(cells)
        if all(c in ("00", "--") for c in cells):
            continue  # lege regel overslaan; het gat is zichtbaar aan de adressen
        print(f"  {base:>5}    {row}")
    print()
    print("  Alleen niet-lege regels. Ontbrekende reeksen staan volledig op nul.")

    wide = {a: v for a, v in w.items() if v > 0xFF}
    if wide:
        print()
        print("  Registers met een hoge byte (dus geen enkele byte-waarde):")
        for a, v in sorted(wide.items()):
            print(f"    {a}  {v}  (0x{v:04X})")


def named(w):
    head("Benoemde adressen")
    for addr, label in LABELS.items():
        v = w.get(addr)
        print(f"  {addr}  {label:<24} {v}")


def blocks(w):
    """Aaneengesloten reeksen met inhoud, zodat je ziet waar de data zit."""
    head("Gevulde reeksen")
    addrs = sorted(a for a, v in w.items() if v != 0)
    if not addrs:
        print("  alles nul")
        return
    start = prev = addrs[0]
    runs = []
    for a in addrs[1:]:
        if a - prev > 4:  # kleine gaten binnen een blok laten we staan
            runs.append((start, prev))
            start = a
        prev = a
    runs.append((start, prev))
    total = 0
    for a, b in runs:
        n = sum(1 for x in range(a, b + 1) if w.get(x))
        total += n
        print(f"  {a}-{b:<6} {b - a + 1:>4} registers, {n:>4} gevuld")
    print(f"  {'':<8} {'':>4}            {total:>4} gevuld van {len(w)} totaal")


def diff(a, b, pa, pb):
    wa, wb = words(a), words(b)
    head(f"Verschillen: HP{a['source']['hp']} tegen HP{b['source']['hp']}")

    ia, ib = a["identity"], b["identity"]
    print("  Identificatie")
    for key in ("official_firmware", "pcb_program", "eeprom_program", "control_board_item"):
        va = ia[key].get("label") or ia[key]["raw"]
        vb = ib[key].get("label") or ib[key]["raw"]
        mark = "  " if va == vb else ">>"
        print(f"  {mark} {key:<24} {va}   |   {vb}")
    for key in ("project_code", "hardware_version", "compressor_code", "odu_dip_switch", "odu_address"):
        mark = "  " if ia[key] == ib[key] else ">>"
        print(f"  {mark} {key:<24} {ia[key]}   |   {ib[key]}")

    print()
    print("  Fingerprints")
    for key in a["eeprom"]["fingerprints"]:
        va, vb = a["eeprom"]["fingerprints"][key], b["eeprom"]["fingerprints"][key]
        mark = "  " if va == vb else ">>"
        print(f"  {mark} {key:<24} {va}   |   {vb}")

    print()
    print("  Registers")
    changed = [x for x in sorted(set(wa) | set(wb)) if wa.get(x) != wb.get(x)]
    if not changed:
        print("    identiek, alle 512 registers")
        return
    print(f"    {len(changed)} van {len(set(wa) | set(wb))} registers verschillen")
    print()
    print(f"    {'blad':>6}  {'HP' + str(a['source']['hp']):>6}  {'HP' + str(b['source']['hp']):>6}   label")
    print("    " + rule("-", 60))
    for x in changed:
        va, vb = wa.get(x), wb.get(x)
        print(f"    {x:>6}  {str(va):>6}  {str(vb):>6}   {LABELS.get(x, '')}")


def main():
    if len(sys.argv) not in (2, 3):
        print(__doc__)
        return 1

    a = load(sys.argv[1])
    identity(a)
    w = words(a)
    named(w)
    blocks(w)
    hexdump(w)

    if len(sys.argv) == 3:
        b = load(sys.argv[2])
        print()
        print(rule("#"))
        identity(b)
        diff(a, b, sys.argv[1], sys.argv[2])
    return 0


if __name__ == "__main__":
    sys.exit(main())
