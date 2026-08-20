#!/usr/bin/env python3
"""Checksum van een ODU EEPROM-dump narekenen, en die van een gewijzigd beeld.

    python crc.py hp1-eeprom.json                 # controleer
    python crc.py hp1-eeprom.json 3309=55         # dry run van een wijziging

De buitenunit slaat een CRC16/Modbus op over de LAGE byte van blad 3000..3509
(510 bytes), little-endian op 3510 (laag) en 3511 (hoog). Init 0xFFFF, polynoom
0xA001. Nagerekend op beide units van de Duo: 0xB191.

Dit is de voorwaarde voor fase 1 van docs/odu-eeprom-parameters.md. Schrijf nooit
een register zonder eerst hier te zien wat de nieuwe checksum moet worden -- een
verkeerde checksum kan de configuratie bij de volgende start ongeldig maken.
"""
import json
import sys

FIRST, LAST = 3000, 3509  # het CRC-bereik; 3510/3511 zijn de checksum zelf


def crc16_modbus(data, init=0xFFFF, poly=0xA001):
    crc = init
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ poly if crc & 1 else crc >> 1
    return crc


def load(path):
    with open(path, encoding="utf-8") as fh:
        dump = json.load(fh)
    return dump, {r["sheet_address"]: r["word"] for r in dump["eeprom"]["registers"]}


def checksum(words):
    return crc16_modbus(bytes(words[a] & 0xFF for a in range(FIRST, LAST + 1)))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    dump, words = load(sys.argv[1])
    stored = (words[3511] << 8) | words[3510]
    calc = checksum(words)

    print(f"HP{dump['source']['hp']}  blad {FIRST}..{LAST}, {LAST - FIRST + 1} bytes")
    print(f"  opgeslagen  0x{stored:04X}")
    print(f"  berekend    0x{calc:04X}   {'match' if calc == stored else 'MISMATCH'}")
    if calc != stored:
        print("  De dump is niet consistent. Niets schrijven.")
        return 2

    edits = {}
    for arg in sys.argv[2:]:
        addr, _, value = arg.partition("=")
        addr, value = int(addr), int(value, 0)
        if not FIRST <= addr <= LAST:
            print(f"  {addr} valt buiten {FIRST}..{LAST} -- de checksum dekt dit niet")
            return 2
        edits[addr] = value

    if not edits:
        return 0

    print()
    print("Dry run -- er wordt niets verstuurd")
    for addr, value in sorted(edits.items()):
        print(f"  {addr}  {words[addr]} -> {value}")

    after = dict(words)
    after.update(edits)
    new = checksum(after)
    print()
    print(f"  nieuwe checksum   0x{new:04X}")
    print(f"    3510 (laag)     {new & 0xFF}")
    print(f"    3511 (hoog)     {new >> 8}")
    print()
    print("  Of je die twee zelf moet schrijven is onbekend -- de unit kan ze ook")
    print("  zelf bijwerken. Fase 1 beantwoordt dat met een schrijfactie die de")
    print("  bestaande waarde terugschrijft. Zie docs/odu-eeprom-parameters.md.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
