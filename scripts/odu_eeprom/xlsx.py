"""Minimale xlsx-lezer: zipfile + ElementTree, geen externe pakketten."""
import re
import zipfile
import xml.etree.ElementTree as ET

NS = "{http://schemas.openxmlformats.org/spreadsheetml/2006/main}"
RNS = "{http://schemas.openxmlformats.org/officeDocument/2006/relationships}"


def _shared_strings(z):
    try:
        root = ET.fromstring(z.read("xl/sharedStrings.xml"))
    except KeyError:
        return []
    out = []
    for si in root.findall(f"{NS}si"):
        # tekst kan over meerdere runs verdeeld zijn
        out.append("".join(t.text or "" for t in si.iter(f"{NS}t")))
    return out


def sheet_names(z):
    wb = ET.fromstring(z.read("xl/workbook.xml"))
    rels = ET.fromstring(z.read("xl/_rels/workbook.xml.rels"))
    target = {r.get("Id"): r.get("Target") for r in rels}
    out = []
    for sh in wb.find(f"{NS}sheets"):
        t = target[sh.get(f"{RNS}id")].lstrip("/")
        if not t.startswith("xl/"):
            t = "xl/" + t
        out.append((sh.get("name"), t))
    return out


def _col(ref):
    m = re.match(r"([A-Z]+)(\d+)", ref)
    letters, row = m.group(1), int(m.group(2))
    c = 0
    for ch in letters:
        c = c * 26 + (ord(ch) - 64)
    return c - 1, row - 1


def rows(z, path, strings):
    """Lijst van rijen, elke rij een lijst cellen (str), rechts afgekapt."""
    root = ET.fromstring(z.read(path))
    grid = {}
    maxc = 0
    for row in root.iter(f"{NS}row"):
        for c in row.findall(f"{NS}c"):
            ref = c.get("r")
            if not ref:
                continue
            ci, ri = _col(ref)
            t = c.get("t")
            if t == "s":
                v = c.find(f"{NS}v")
                val = strings[int(v.text)] if v is not None else ""
            elif t == "inlineStr":
                val = "".join(x.text or "" for x in c.iter(f"{NS}t"))
            else:
                v = c.find(f"{NS}v")
                val = v.text if v is not None else ""
            val = (val or "").strip()
            if val:
                grid[(ri, ci)] = val
                maxc = max(maxc, ci)
    if not grid:
        return []
    maxr = max(r for r, _ in grid)
    out = []
    for r in range(maxr + 1):
        out.append([grid.get((r, c), "") for c in range(maxc + 1)])
    return out


def open_book(path):
    z = zipfile.ZipFile(path)
    return z, _shared_strings(z)
