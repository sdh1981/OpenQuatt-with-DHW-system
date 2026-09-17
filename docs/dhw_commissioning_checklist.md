# DHW Commissioning Checklist

Met deze checklist krijg je de DHW-regeling eerst stabiel en reproduceerbaar werkend, en ga je pas daarna optimaliseren.

Hoe alles werkt en wat elke instelling doet: [DHW: werking en instellingen](dhw-instellingen.md).

## 1. Hardware

- [ ] Relais-uitgangen gecontroleerd met `DHW manual test mode`:
  - [ ] 3-wegklep: `DHW manual valve relay` (GPIO40)
  - [ ] Element/contactor: `DHW manual element relay` (GPIO48)
- [ ] Klep-hulpcontact: `DHW valve aux (DHW position)` is aan in DHW-stand en uit in CV-stand
- [ ] Element op een eigen fase (L2), gescheiden van de HP's (L1), met geschikte zekering en contactor
- [ ] `DHW manual test mode` weer **uit**

## 2. Sensoren (CWT-TM-8PT, Modbus 3)

- [ ] Kanaal 1 → `DHW tank top`, kanaal 2 → `DHW tank bottom` (niet omgedraaid: de top is warmer)
- [ ] Kanaal 3 → `DHW coil in`, kanaal 4 → `DHW coil out`, plausibel en stabiel
- [ ] Alle vier geven **live** waarden van de module, niet de vaste terugval
  - Terugval: `DHW source tank top` 50, `tank bottom` 45, `coil in` 35, `coil out` 30 °C
  - Een waarde die exact op zo'n getal blijft staan, betekent dat module en HA-proxy allebei ontbreken; er volgt dan géén sensorfout
- [ ] Flow plausibel tijdens een HP-run
- [ ] HP-fout-input (HA-proxy of `DHW source HP fault`) en `DHW lockout` getest

## 3. Toestandsmachine

- [ ] `IDLE_CV → DHW_PREPARE → DHW_HEAT_PUMP` bij tanktop onder `DHW start top`
- [ ] Klep bevestigd binnen 20 s, anders `VALVE_STUCK_CV`
- [ ] HP-fase stopt op **tankbodem 52 °C** (vast in de firmware)
- [ ] Natraject `DHW_BOOST` start alleen als de tanktop onder `DHW boost target` zit en `DHW boost after HP` aan staat; element stopt op het doel
- [ ] `DHW boost now` (snelboost): beide HP's en element; uitzetten breekt direct af
- [ ] Legionella geforceerd met `DHW source legionella force`:
  - [ ] HP stopt op bodem 53 °C of top 55 °C
  - [ ] element gaat door tot 68 °C
  - [ ] 15 min hold
  - [ ] `DHW legionella laatste run` bijgewerkt

## 4. Warmtepompen (Duo)

- [ ] Duo DHW: beide units op `DHW HP level`, `Request Reason` = `dhw_duo`
- [ ] Single-HP mode (als gebruikt):
  - [ ] `DHW single HP lead` toont de lead
  - [ ] De lead blijft de hele cyclus dezelfde
- [ ] Zachte aanloop: de compressor begint op niveau 1 en klimt per `DHW soft start step time`
- [ ] Tweede-HP assist (als gebruikt): `DHW second HP assist status` volgt de verwachte stappen, en de starts van de assisterende unit blijven beperkt

## 5. Interlocks en veiligheid

- [ ] Element nooit aan zonder bevestigde DHW-klepstand (behalve CM6)
- [ ] `VALVE_MISMATCH` na 10 s klep in CV terwijl DHW actief is
- [ ] `FLOW_OUT_OF_RANGE` na 30 s buiten `DHW flow min` (750) / `DHW flow max` (1800 l/h)
- [ ] Reboot tijdens een cyclus: klep naar CV, element uit, geen spontane legionella-run
- [ ] Watertemperatuur-trip geeft `HP_FAULT`, die met de hand gewist moet worden

## 6. Fouten

- [ ] `DHW clear fault` wist de fout
- [ ] Fout blijft weg als de oorzaak weg is
- [ ] Fout komt terug als de oorzaak blijft (verwacht gedrag)
- [ ] Een boost die 90 min haalt, geeft géén fout maar telt op in `DHW boost timeouts`

## 7. Dashboard en instellingen

- [ ] Alle gebruikte DHW-entiteiten bestaan
- [ ] `DHW auto boost enable` bewust gekozen (standaard uit)
- [ ] Legionelladoel past bij de boiler (Inventum: 68 °C)
- [ ] `DHW flow min`/`max` afgesteld op de installatie
- [ ] Instellingen vastgelegd als ijkpunt (zie [Instellingen-vangnet](config-snapshot-v0.60.md))

## 8. Duurtest

- [ ] 24–48 uur zonder handmatige ingreep
- [ ] Geen onverwachte gelatchte fouten
- [ ] Toestandsovergangen en logs gecontroleerd
- [ ] `DHW cyclus COP` per cyclus vastgelegd als referentie

## 9. Baseline

- [ ] Werkende versie gecommit/getagd
- [ ] Korte changelog
- [ ] Testresultaten bewaard
