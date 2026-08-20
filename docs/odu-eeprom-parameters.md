# ODU EEPROM: wat er in staat, en wat schrijven zou kosten

Vastgelegd 17 augustus 2026, naar aanleiding van de eerste volledige EEPROM-dumps van beide buitenunits.

> **Stand van zaken.** Sinds v0.62.0 kan één ding wél geschreven worden: de frequentiekrommen, tijdelijk, via [de runtime-frequentietabel](#gebouwd-in-v0620-de-frequentiekromme-tijdelijk-zetten) onderaan dit bestand. Dat raakt de runtime-schaduw en niet de chip. **Het voorstel hieronder gaat over schrijven náár de EEPROM zelf, en dat is nog steeds niet gebouwd** — de vier onbekenden staan nog open.

## Wat er is gemeten

Twee dumps, HP1 en HP2, gemaakt met de knop `HP1 - ODU EEPROM dump starten` uit `openquatt_odu_eeprom_dump`. Beide compleet, checksum `0xB191`, nul herleespogingen, elk in ~11,6 s.

| | |
|---|---|
| Firmware ODU | 1.30 |
| PCB-programma | V001_T30 |
| EEPROM-programma | 114 |
| Regelprint / projectcode | 3639 (`0x0E37`) |
| Koudemiddel | 0 (R32/R410A) |
| Model, klantmodel, serienummer | leeg — 20× `0xFFFF`, nooit beschreven |

**Alle 512 registers zijn byte-identiek tussen HP1 en HP2.** Inclusief de 194 die het document als "Internal parameters" aanduidt. Het enige verschil zit in het runtime-kernblok: DIP `0x1800` tegen `0x1801` — bit 0 is "Unit address 0", dus enkel het adresverschil — en verdamperdruk 14,8 tegen 14,6 bar bij gelijke condensordruk 14,7, wat bij stilstand hoort.

Van de 254 gevulde registers zijn er **60 benoemd** in het protocoldocument. De rest is officieel ongedocumenteerd.

### Leesbaar geworden

De frequentiekrommen, en die zijn het interessantst:

| | F0 | F1 | F2 | F3 | F4 | F5 | F6 | F7 | F8 | F9 | F10 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Koelen (3001–3011) | 0 | 30 | 36 | 42 | 47 | 52 | 56 | 61 | 66 | 71 | 74 |
| Verwarmen (3012–3022) | 0 | 30 | 39 | 49 | 55 | 61 | 67 | 72 | 79 | 85 | 90 |

Verder: EEV-regelmodus PID, baudrate 19200, bodemplaat- en carterverwarming (start bij 34 resp. 33, hysterese 3), drukbeveiliging afslag 41 en 5 bar, ontdooien (in 27, uit 47, max 8 min, standby 30 Hz, draaiend 61 Hz), en de pomp- en ventilatorvermogens 45/20/10 en 35/20/10.

### Drie waarden buiten het gedocumenteerde bereik

```
3308  Low noise mode switch     = 5    doc: 0=No Enable, 1=Enable
3459  Water flow sensor type    = 3    doc: 0=No Enable, 1=sika, 2=huba
3497  DC water pump fo enable   = 2    doc: 0=No Enable, 1=Enable
```

Alle drie zijn enums, op beide units gelijk. Dat wijst op een document dat ouder is dan firmware 1.30, niet op een verkeerd ingestelde unit.

## Wat dit over de flowmeting zegt

Doc-adres 2139 is OpenQuatt-register 2138 en heet daar **"Water flow", bereik 5–80 L/min**. Geen millivolts. Bij 1100 L/h staat raw op ~1780, wat 17,8 L/min is — raw is dus honderdsten van een liter per minuut.

De rustwaarde van 547 L/h is raw 885, oftewel **8,85 L/min**. Dat zit net boven `3456 Min. flow rate` = 8 en ruim binnen het meetbereik. Er staat geen nul in de specificatie: een vortexsensor onder zijn ondergrens heeft geen "geen flow"-toestand, en de buitenunit heeft er ook geen voor gedefinieerd.

Dat verklaart in één keer waarom een nieuwe sensor op HP1 het doet, waarom de originele sensor van HP2 het inmiddels ook doet, en waarom andere OpenQuatt-gebruikers hetzelfde zien. Het is geen defect.

### Een openstaand punt over de schaalfactor

Als raw honderdsten L/min is, dan is L/h gelijk aan raw × **0,6**. OpenQuatt gebruikt **0,618** in [`oq_HP_io.yaml`](../openquatt/oq_HP_io.yaml), en die factor komt nergens in de repo met een onderbouwing voor; de CiC-brug rekent hem consequent terug. Dat is 3 % te hoog en werkt door in `heat_power` en in elke COP.

Bewust niet aangepast. Het kan een ijking tegen een echte meter zijn die het document overstijgt.

## Voorstel: parameters schrijven

### Wat vaststaat

- Het document markeert het 3000-blok als **schrijfbaar**: de Code-kolom leest als 37788, maar dat is Excel die de tekst "03/6/16" als datum heeft ingeslikt — dezelfde functiecodes als het 2000-blok, dus 3 lezen, 6 één register, 16 meerdere.
- De checksum is **onafhankelijk na te rekenen**: CRC16/Modbus, init `0xFFFF`, polynoom `0xA001`, over de lage byte van blad 3000..3509, little-endian op 3510/3511. Zie [`scripts/odu_eeprom/crc.py`](../scripts/odu_eeprom/crc.py).
- De **terugweg bestaat**: twee byte-exacte, checksum-geldige dumps, en HP2 kan fabrieksreferentie blijven.
- OpenQuatt meldt al "EEPROM failure" uit register 2121 bit 7 — dat is de afbreekdetector, al gebouwd.
- Register 2100 documenteert bedrijfsmodus **101: EEPROM data writing**, dus de unit heeft hier een eigen toestand voor.

### Wat we niet weten

1. Werkt de unit zijn checksum zelf bij, of moeten wij 3510/3511 meeschrijven?
2. Moet de unit eerst in modus 101, en hoe kom je daar? Het 2000-blok heeft geen zichtbaar commando.
3. Landt een schrijfactie in de EEPROM of alleen in de runtime-schaduw? Onze dump heet `runtime_eeprom_shadow`.
4. Schrijft de Quatt CiC zelf ook in dit blok?

De eerste twee hebben tegengestelde gevolgen als je verkeerd gokt.

### Het werkelijke risico

Niet een verkeerde waarde, maar een verkeerde checksum in combinatie met een onbetrouwbaar schrijfpad. Dat laat een buitenunit achter die niet start en geen weg terug heeft, op een cv-installatie in een bewoond huis. Daarom bewijst fase 1 de terugweg vóórdat hij nodig is.

### Waar schrijven iets oplevert

| Register | Nu | Opbrengst | Risico |
|---|---|---|---|
| `3309` Low noise FAN speed | 62 | hoorbaar en meetbaar in 2105/2106 | laag, op zichzelf staand |
| `3001–3022` frequentiekrommen | zie tabel | herdefinieert wat "level N" in Hz betekent — de grootste tuningwinst | hoog: perf_map, beveiligingen en de levellogica gaan hiervan uit |
| `3277–3281` ontdooien | in 27, uit 47, max 8 | winst als ontdooien slecht loopt | ontdooien is waar je dingen sloopt |
| `3238` / `3240` verwarmingslinten | 34 / 33 | standby-verbruik | vorstbeveiliging |
| `3247` / `3248` drukbeveiliging | 41 / 5 bar | — | nooit |

**Niet voor de flowmeting.** Er is geen register dat "meld nul onder X" betekent. `3495 Water flow correction value` staat op 0, maar een offset van 8,9 L/min haalt ook 530 L/h van elke echte meting af. `3459` sensortype niet aankomen: waarde 3 is ongedocumenteerd, en een andere waarde legt de overdrachtsfunctie van een ander merk op alle metingen én op de beveiligingen van de unit zelf. `3456` is een beveiligingsdrempel, geen weergavefilter.

Het filteren van een meting onder het bereik hoort waar OpenQuatt het al doet: `oq_flow_phantom_suppression_enable`, dat in CM0 naar 0 forceert. Risicoloos en omkeerbaar.

### Fasering

**Fase 0 — bouwen, niet schrijven.** Een schrijfblok in de diagnostiek: adres, waarde, uitvoerknop, standaard uit. Harde grenzen in de lambda: alleen in CM0, één register per keer, verbodslijst op 3000, 3247, 3248, 3456, 3459, 3510 en 3511. Plus een dry run die het gewijzigde beeld en de nieuwe checksum toont zonder iets te versturen — dat deel bestaat al in `crc.py`.

**Fase 1 — bewijzen dat schrijven werkt, zonder iets te veranderen.** Schrijf 62 naar `3309`, de waarde die er al staat. Daarna opnieuw dumpen.

- Geslaagd: checksum nog `0xB191`, register onveranderd, 2121 bit 7 blijft nul.
- Afbreken: Modbus-exception, gewijzigde checksum, of die foutbit.

Dit beantwoordt vraag 1 en 3 in één keer.

**Fase 2 — één echte wijziging, alleen op HP1.** `3309` van 62 naar 55; HP2 blijft fabrieksinstelling. Eén verwarmingscyclus observeren, dan terugzetten.

**Fase 3 — beslissen.** Terug naar fabriek, of met een uitgewerkt plan naar de frequentiekrommen.

### Vaste regels

- Nooit schrijven met een draaiende compressor. Alleen in CM0.
- Altijd dumpen voor en na. Elke dump bewaren.
- Eén register per stap.
- HP2 blijft onaangeroerd zolang HP1 het proefdier is.
- Dit valt buiten alles wat Quatt ondersteunt.

### Advies

Fase 0 en 1 zijn goedkoop en leveren het antwoord op de vraag of dit kan — waardevol los van elk doel. En als het ooit doorgaat: dit doe je in de zomer, niet in januari.

## Gebouwd in v0.62.0: de frequentiekromme tijdelijk zetten

> **De schrijfgrendel staat open.** `odu_freq_write_unlocked` staat op `"1"` voor een actieve afstelperiode. Op `"0"` weigert "tabel toepassen" meteen en komt er niets op de bus.
>
> Er liggen drie lagen achter: de vrijgaveschakelaar, de schakelaar voor schrijven tijdens bedrijf, en de grens van 5 Hz per stand per keer. De eerste twee staan na elke herstart weer uit, dus schrijven blijft een reeks bewuste handelingen. Zet de substitutie terug op `"0"` zodra het afstellen klaar is.

Overgenomen van upstream, waar dit in `openquatt/experimental/` staat. Het schrijft **22 registers vanaf modbus 3000** — koelen F0–F10 en verwarmen F0–F10, blad 3001 tot en met 3022 — in één functie-16 transactie. Verder wordt er niets aangeraakt.

Het is vluchtig: dit gaat naar de runtime-schaduw en de checksum op 3510/3511 blijft staan, dus een volledige power cycle van de buitenunit zet de fabriekstabel terug. Dat is meteen de noodrem.

### Bewaking, in volgorde

1. weigert als er een EEPROM-dump loopt
2. weigert als de vrijgaveschakelaar uit staat
3. valideert de ingevoerde tabel: elke waarde 0–120 Hz **en oplopend**
4. leest modbus 2099–2103 en weigert tenzij **werkmodus 0** én **compressorfrequentie 0**
5. zet de vrijgaveschakelaar zelf uit, dán pas schrijven
6. leest de 22 registers terug en vergelijkt

Stap 4 is de kern: er wordt nooit geschreven op een draaiende machine. Stap 5 maakt het spannen-en-vuren.

### Bediening

`tabel ophalen` → invoervakken aanpassen → `schrijven vrijgeven` → `tabel toepassen`. De status volgt het hele pad, van `OPGEHAALD: 22/22 registers` tot `TOEGEPAST` of `GEBLOKKEERD: compressor draait`.

Entiteiten staan in Home Assistant onder Configuratie en Diagnostiek, en in de web-UI in een eigen groep onderaan. Bij upstream zijn ze `internal` omdat hun webapp ze aanstuurt; die bouwt deze fork niet mee.

### Waar dit voor gebruikt wordt

Het uitgewerkte experiment staat in [dynamische-compressorfrequentie-koelen.md](dynamische-compressorfrequentie-koelen.md): stand vastzetten op 1 en de frequentie erachter variëren, om te meten hoe laag deze machine houdbaar kan draaien. Koelen, omdat de koelstrategie de perf_map niet gebruikt en de condensbeveiliging op een gemeten temperatuur werkt.

### Wat je hierbij moet weten

**`hp_perf_map.h` klopt niet meer zodra je de tabel verschuift.** Die bevat gemeten vermogens en COP's per stand en voedt de duo-dispatcher, de vermogensberekening en elke COP. Verschuif je de Hz achter een stand, dan liegt die tabel. Gebruik dit voor korte, bewaakte proeven — niet als instelling die blijft staan.

**30 Hz lijkt de ondergrens van deze compressor.** Die waarde komt op drie onafhankelijke plekken in de EEPROM voor: koelen F1, verwarmen F1 en de ontdooi-standbyfrequentie. Eronder duiken betekent onder de waarde gaan die de fabrikant overal als bodem aanhoudt, op een compressor waarvan `compressor_code` 0 is en we het type dus niet kennen. Olieretour bij lage toerentallen is een echte faalwijze.

**De V2-getallen zijn niet overdraagbaar.** Die unit draait R290, deze R32. Frequenties vergelijken over koudemiddelen heen zegt niets over capaciteit.

## Gereedschap

In [`scripts/odu_eeprom/`](../scripts/odu_eeprom/):

| | |
|---|---|
| `eeprom_report.py` | dump leesbaar maken, met hexdump en gevulde reeksen |
| `decode.py` | dump tegen het protocoldocument leggen, en twee dumps vergelijken |
| `crc.py` | checksum controleren en de checksum van een gewijzigd beeld berekenen |
| `xlsx.py` | minimale xlsx-lezer, geen externe pakketten nodig |

Het protocoldocument zelf zit niet in de repo. Zet het pad in `OQ_MODBUS_XLSX` of leg het naast de scripts. Op Windows `PYTHONIOENCODING=utf-8` meegeven, anders struikelt de console over de Chinese bladnamen.

```bash
PYTHONIOENCODING=utf-8 python scripts/odu_eeprom/decode.py hp1-eeprom.json hp2-eeprom.json
```
