# Innova AirLeaf via Modbus RTU op een LilyGo T-CAN485

ESPHome-configuratie voor een of meer Innova AirLeaf SL 400-fancoils (ESE645II / INN-FR-B32) op
één RS485-bus. Het resultaat in Home Assistant is per fancoil een `climate`-entiteit met aparte
sensoren. Er zit geen eigen regeling in: de Innova-print regelt de ventilator zelf (PI).

| Bestand | Inhoud |
|---|---|
| `airleaf-fancoils.yaml` | Board, WiFi/OTA/fallback-AP, UART, Modbus-bus, lijst met fancoils |
| `packages/innova_fancoil.yaml` | Alles voor één fancoil (registers, watchdog, entiteiten), herbruikbaar via `vars` |
| `components/innova_airleaf/` | Kleine climate-component: vertaalt HA-opdrachten naar registerschrijfacties via drie interne `modbus_controller`-numbers (201/231/233) |
| `secrets.example.yaml` | Sjabloon voor `secrets.yaml` |

Gecontroleerd met ESPHome 2026.9.1: `esphome config` en `esphome compile` slagen, ook met twee
fancoils ingeschakeld. Op echte hardware is niets getest.

## Bronnen

- **Registers:** Innova N273025C rev.01, *Installation, connection and settings wall mounted control
  EDA649, EDB649, ECA644, ECA647 for use in a Modbus RTU serial network*. Beide referentieprojecten
  bevatten precies dit document (`jjdejong/esphome-airleaf/doc/…bridge_modbus_rtu…pdf` en
  `lorenzo93/homeassistant-innova/docs/Innova-modbus-registries.pdf` zijn byte-identiek).
- **Hoe de registers in de praktijk gebruikt worden:** `jjdejong/esphome-airleaf` (`Airleaf.yaml`,
  ECA644 + D1 mini) en `lorenzo93/homeassistant-innova` (`climate.py`, INN-FR-B32 via Modbus TCP).
- **Board:** `Xinyuan-LilyGO/T-CAN485`: `examples/RS485/RS485.ino`, `examples/Original_Test`,
  schema `project/T-CAN485.pdf` en het MAX13487E-datasheet in `information/`.

## 1. Bedrading

### LilyGo T-CAN485: wat intern al vastligt

| ESP32 | Functie | Instelling in de config |
|---|---|---|
| GPIO22 | RS485 TX → MAX13487E DI | `uart.tx_pin` |
| GPIO21 | RS485 RX ← MAX13487E RO | `uart.rx_pin`, met pull-up |
| GPIO17 | MAX13487E **RE** | altijd hoog |
| GPIO19 | MAX13487E **SHDN** | altijd hoog (transceiver aan) |
| GPIO16 | ME2107 5V-boost aan/uit | altijd hoog |

**Er is geen DE-pin.** De MAX13487E heeft *AutoDirection*: de chip zet de driver zelf aan zodra
DI laag wordt en weer uit na het frame. Met RE hoog regelt de AutoDirection-schakeling ook de
ontvanger, dus u krijgt uw eigen verzonden bytes niet als echo terug (datasheet, tabel
"Receiving"). Daarom heeft `modbus:` geen `flow_control_pin`. Zou RE laag staan, dan leest
ESPHome zijn eigen verzoek terug en faalt elke transactie.

> **VERIFIEREN (board-revisie):** de pinouttabel in de LilyGo-README noemt "EN = IO9". GPIO9 is
> de interne flash van de ESP32-module en kan dat niet zijn. De voorbeeldcode en de netnamen in
> het schema (`RS485_SE`, `RS485_EN`) wijzen op GPIO19 en GPIO17. Komt er bij stap 3 van de
> testprocedure echt niets binnen, meet dan met de voeding uit de doorgang tussen GPIO19 en
> pin 3 (SHDN) van de MAX13487E, en tussen GPIO17 en pin 2 (RE).

### Naar de fancoil

| LilyGo-klem | Innova-klem | Opmerking |
|---|---|---|
| RS485 **A** | **A** | |
| RS485 **B** | **B** | |
| **GND** | **−** | VERIFIEREN, zie hieronder |
| — | **+** | **niet aansluiten** |
| 5–12 V / GND (voedingsklem) of USB-C | — | aparte 5–12 V-voeding voor de LilyGo |

- Gebruik getwiste, afgeschermde tweeaderige kabel van minstens 0,35 mm² (Innova), maximaal
  500 m, als lijn en niet in ster. Leg de kabel niet naast 230 V-bekabeling.
- **Afsluiting:** Innova vraagt 120 Ω aan het einde van de lijn. In het T-CAN485-schema staat een
  120 Ω-weerstand bij de RS485-uitgang. **VERIFIEREN** of die vast is aangesloten of via een
  jumper/soldeerbrug: meet met alles spanningsloos de weerstand tussen A en B op de LilyGo zonder
  kabel eraan. Rond 120 Ω betekent dat de afsluiting op het board zit, en dan hoort de LilyGo aan
  één uiteinde van de bus. Aan het andere uiteinde (de laatste fancoil) komt 120 Ω tussen A en B.
- **VERIFIEREN klem "+ A B −" op de Innova:** het Innova-document noemt de klemmen alleen
  "+AB- ModBus RTU serial connection" en zegt niet wat + en − zijn. Sluit **+ nooit** aan, want
  daar kan voedingsspanning voor een wandbediening op staan. Meet eerst met een multimeter,
  terwijl de fancoil aan staat, de gelijkspanning tussen − en de GND van de LilyGo **voordat u ze
  verbindt**. Is dat ongeveer 0 V of zweeft het zonder duidelijk potentiaal, verbind dan GND met
  −. Staat er een onverwachte spanning, laat GND dan weg en gebruik alleen A/B. Werk aan de
  fancoilprint alleen spanningsloos; het is een 230 V-print.
- **A/B omgewisseld?** Fabrikanten benoemen A en B niet allemaal hetzelfde. Krijgt u alleen
  time-outs terwijl de rest klopt, wissel dan A en B. Bij RS485 kan dat geen kwaad.
- **Waar zit de Modbus-poort?** Volgens het Innova-document zit die bij een EDA649/EDB649-
  wandbediening **op de wandbediening**, met de klemmen "+ A B −". De klemmen "A A B B −"
  daarnaast zijn de interne verbinding naar de fancoilprint en daar sluit u niets op aan. Bij
  losse INN-FR-B32-printen zit de "+AB−"-klem op de print zelf. Controleer met welke van de twee
  opstellingen uw ESE645II werkt, want dat bepaalt ook welke registers bestaan (zie hieronder).

## 2. Modbus-instellingen

| Instelling | Waarde | Waar instellen |
|---|---|---|
| Baudrate | 9600 (Innova) | `substitutions.modbus_baud_rate` |
| Frame | 8N1 (Innova) | `modbus_parity` (NONE/EVEN/ODD), `modbus_stop_bits` |
| Functiecodes | 03 lezen, 06 schrijven, één register per keer | vast |
| Adresoffset | geen: registernummer = PDU-adres | vast |
| Slave-adres | per fancoil, variabele `fc_address` | `packages:` in de hoofdconfig |
| Antwoord-time-out | 500 ms | `modbus_send_wait_time` |
| Rust tussen frames | 100 ms | `modbus_turnaround_time` |

Innova adviseert om **adres 001 vrij te houden** voor vervanging of uitbreiding en de fancoils
vanaf 002 te nummeren. Het adres stelt u in op het toetsenbord: zet de unit in stand-by, houd
AUTO 5 s ingedrukt, kies het adres met de pijltjestoetsen en bevestig met stand-by of door 10 s
te wachten. Het kan ook via register 200, maar deze config schrijft 200 bewust niet.

## 3. Gebruikte registers

R = lezen met FC03, W = schrijven met FC06. Registers met **\*** staan in de Innova-tabel als
*"only for INN-FR-B32 board"*.

| Reg | Naam | R/W | Schaal / eenheid | Betekenis | Entiteit |
|---|---|---|---|---|---|
| 000 | T1 | R | ×0,1 °C | Luchttemperatuur, ook de hartslag van de watchdog | `…kamertemperatuur`, climate current |
| 001\* | T2 | R | ×0,1 °C | Watertemperatuur in de batterij (probe H2) | `…watertemperatuur` |
| 008 | SP | R | ×0,1 °C | Actief setpoint, inclusief E_SAVING-offset | `…actief setpoint` |
| 009\* | OUT | R | bitveld | b0 EV1, b1 EV2, b2 CHILLER, b3 BOILER | `…boilervraag`, `…chillervraag`, `…waterklep EV1` |
| 015\* | MOT_SET | R | rpm (0–1700) | Gevraagd ventilatortoerental | `…ventilatortoerental`, climate-actie |
| 104\* | STAT | R | bitveld | Actieve modus, ventilatorstop door ongeschikt water, vorst, stand-by, master-time-out, probe afwezig | `…status` |
| 105 | ALR_STAT | R | bitveld | Alarmen: probes, motor, filter, water … | `…alarmen`, `…alarm` |
| 201 | PRG | R/W | bitveld | b0–2 programma (000 auto, 001 stil/MIN, 010 nacht, 011 max), b4 LOCK, b7 stand-by, **b8–15 gereserveerd** | climate mode (uit) + fan mode |
| 231 | SP | R/W | ×0,1 °C | Absoluut setpoint, binnen SPL..SPH | climate target, `…doeltemperatuur` |
| 233 | Man | R/W | 0 / 3 / 5 | Seizoen: 0 auto, 3 winter (verwarmen), 5 zomer (koelen) | climate mode |

Deze registers staan in het document maar worden bewust niet gebruikt: 002 (T3, alleen ECA647),
200 (adres), 202/203 (SPL/SPH), 209 (E_SAVING), 210–215, 230 en 234 (toerentalgrenzen per
programma), 218/219 (watertemperatuurgrenzen), 221/222 (onderhoud), 242–244 (probe-offsets),
245–247 (WEB-grenzen/flags). U voegt ze toe door een blok in `packages/innova_fancoil.yaml` te
kopiëren.

### Koppeling naar Home Assistant

| HA | Registers |
|---|---|
| climate **uit** | 201 bit 7 = 1 (stand-by). Seizoen en programma blijven staan |
| climate **verwarmen** | 233 = 3, daarna 201 bit 7 = 0 |
| climate **koelen** | 233 = 5, daarna 201 bit 7 = 0 |
| climate **heat_cool** | 233 = 0 (seizoen automatisch). Staat erin om die toestand correct te kunnen tonen, u hoeft hem niet te gebruiken |
| fan **auto** | 201 bit 0–2 = 000: modulerend tussen MVV4 en MVV2 (550–1100 rpm af fabriek) |
| fan **quiet** | 010 = nacht, MVV5–MVV4 (400–550) |
| fan **low** | 001 = stil/MIN, MVV5–MVV3 (400–680) |
| fan **high** | 011 = maximum, MVV3–MVV1 (680–1500) |
| target temperature | 231 = round(T×10), stap 0,5 °C, begrensd op `fc_sp_min`..`fc_sp_max` |
| actie | uit bij stand-by, idle als MOT_SET = 0, anders verwarmen/koelen |

"Stil" staat onder `low` en niet onder `medium`, omdat het MIN-programma met maximaal 680 rpm
lager draait dan auto. De ESE645II kent geen echte middenstand.

## 4. Foutafhandeling

De Innova-print stuurt bij een fout geen Modbus-exception en antwoordt dan gewoon niet. Daarom:

1. **Elke read is een aparte transactie** (`reuse_previous_range: false`). Een niet-bestaand
   register, bijvoorbeeld een \*-register op een print zonder INN-FR-B32-functies, laat dan alleen
   die ene read time-outen en niet een heel blok. jjdejong ontdekte hetzelfde en werkt op dezelfde
   manier.
2. **modbus_controller**: na 2 mislukte herhalingen markeert ESPHome de slave offline, logt
   `Modbus slave N reageert niet` en slaat 2 pollrondes over. Zo houdt één dode fancoil de bus
   niet bezet.
3. **Watchdog per fancoil**: elke geslaagde read van register 000 legt een tijdstempel vast. Komt
   er `fc_offline_s` lang (standaard 45 s) geen antwoord, dan gebeurt het volgende:
   - alle sensoren van die fancoil krijgen `NaN` en de binaire sensoren worden *invalidated*. In
     HA staan ze dan op **"onbekend"** en blijven er geen oude waarden staan. Tekstsensoren kennen
     geen "onbekend" en tonen daarom `geen verbinding`;
   - de climate-entiteit toont huidige en doeltemperatuur als onbekend, en HA-opdrachten worden
     geweigerd en gelogd in plaats van blind verstuurd;
   - `…Modbus-verbinding` (connectivity) gaat uit;
   - zolang de storing duurt, komt er elke minuut een `WARN` in de log met slaveadres en het
     aantal seconden sinds het laatste antwoord.
4. **Read-modify-write op PRG (201)**: bit 8–15 zijn systeemflags die u volgens Innova niet mag
   wijzigen. De component schrijft 201 alleen als de huidige waarde na de laatste (her)verbinding
   gelezen is, en laat alle bits behalve 0–2 en 7 ongemoeid.
5. **Geen schrijfacties bij het opstarten.** De climate zet na een herstart geen opgeslagen
   toestand terug en schrijft alleen als HA iets verandert, en dan alleen als de nieuwe waarde
   afwijkt van de gelezen waarde.

Over "unavailable": ESPHome kan één entiteit niet los *unavailable* maken, want dat doet HA alleen
als het hele apparaat wegvalt. Wat hier gebeurt, **onbekend** plus een connectivity-sensor, komt
daar het dichtst bij. Wilt u in HA echt *unavailable*, zet dan in een template-entiteit
`availability: "{{ is_state('binary_sensor.<fancoil>_modbus_verbinding', 'on') }}"`.

## 5. Testen vóór de fancoil gaat draaien

Doe het in deze volgorde en ga pas verder als een stap klopt.

1. **Alleen de LilyGo, geen bus.** Flash via USB. Controleer in de log: WiFi verbonden, bij
   `modbus` staan de timingwaarden, en per fancoil verschijnt na 45 s
   `nog nooit geantwoord (bedrading, adres, baudrate/pariteit?)`. Zo weet u zeker dat de
   watchdog werkt.
2. **Fancoil in stand-by zetten** met het toetsenbord of de wandbediening, en de spanning eraf.
   Sluit A, B en eventueel GND aan (zie hierboven) en zet de spanning er weer op. Laat de unit in
   stand-by staan. Het uitlezen doet niets aan de fancoil, en zolang de unit in stand-by staat
   gaat hij ook niet draaien als er per ongeluk iets geschreven wordt.
3. **Alleen lezen controleren.** Zet eventueel `modbus_controller.sensor: DEBUG` in `logger.logs`.
   - De kamertemperatuur moet overeenkomen met het display of met een thermometer naast de unit.
   - `…doeltemperatuur` (231) = het setpoint op de wandbediening.
   - PRG (201): zet dit tijdelijk op `internal: false` of bekijk het in de DEBUG-log. In stand-by
     hoort bit 7 gezet te zijn (waarde ≥ 128), bijvoorbeeld 128 = stand-by + auto.
   - Seizoen (233) = 3 of 5, afhankelijk van wat er op het display staat.
   - Alleen time-outs? Controleer dan achtereenvolgens adres, A/B omwisselen, 8N1 en 9600 baud,
     en RE/SHDN (zie VERIFIEREN bij de pinnen).
4. **Watertemperatuur (VERIFIEREN, belangrijk voor de delta-T-plannen).**
   - Onbekend terwijl de andere waarden wel binnenkomen, met de log vol time-outs op register 1:
     uw opstelling levert de \*-registers niet. Dat gebeurt waarschijnlijk als de Modbus-poort op
     een EDA649/EDB649-wandbediening zit en niet op een INN-FR-B32. Zet `water_temp`, `fan_rpm`,
     OUT en STAT dan uit door hun blokken in het package weg te halen, en meet de
     batterijtemperatuur op een andere manier (bijvoorbeeld een DS18B20 op de aanvoer).
   - Een waarde die niet verandert of 0 is, terwijl STAT bit 13 ("H2-probe afwezig") gezet is:
     het register bestaat, maar er hangt geen H2-probe aan.
   - Klopt het: laat warm water door de batterij lopen, dan hoort de waarde binnen een paar
     minuten richting de aanvoertemperatuur van de Quatt te gaan.
5. **Communicatieverlies nabootsen.** Trek A of B los. Na maximaal ongeveer 45 s moeten alle
   waarden van die fancoil op onbekend staan, `Modbus-verbinding` uit, en moet er een WARN in de
   log komen. Sluit A of B weer aan: binnen ongeveer 30 s is alles terug.
6. **Eén schrijfactie, met de unit nog in stand-by.** Verander in HA de doeltemperatuur met
   0,5 °C. Controleer in de log `schrijf register 231 = …`, op het display het nieuwe setpoint en
   in register 231 bij de volgende poll dezelfde waarde. Zet het setpoint daarna terug.
7. **Pas dan inschakelen.** Zet de climate op verwarmen. In de log moet `schrijf register 201`
   staan met als nieuwe waarde de oude min 128, en eventueel eerst `233 = 3`. Controleer dat de
   ventilator binnen de verwachte band start. Probeer daarna de fanstanden.
8. **Meerdere fancoils:** geef elke unit een eigen adres en haal het commentaar voor `fancoil_2` en
   `fancoil_3` in de hoofdconfig weg. Trek bij stap 5 de voeding van één unit: de andere units
   moeten gewoon online blijven.

## 6. Later: delta-T-sturing

De architectuur laat hier ruimte voor:

- De watertemperatuur is per fancoil een gewone sensor met een vaste id: `fc1_water_temp`,
  `fc2_water_temp`, enzovoort. Een regelaar kan die in ESPHome zelf gebruiken (een `interval` of
  een extra component in `components/`) of in HA/OpenQuatt via de API.
- De ventilator kan op twee manieren gestuurd worden. **(a)** Het programma wisselen via PRG
  (4 banden). Dat is grof maar veilig. **(b)** De toerentalgrenzen MVV1–MVV5 (210–215) verschuiven,
  zodat de interne PI binnen een door u gekozen band blijft.
- **Let op, VERIFIEREN vóór u dit bouwt:** Innova schrijft dat instellingen *een
  stroomonderbreking overleven*. Ze staan dus vrijwel zeker in EEPROM, en EEPROM heeft een beperkt
  aantal schrijfcycli. Een regelaar die elke minuut 210–215 of 201 herschrijft, kan de print in
  een paar jaar versleten hebben. Schrijf dan alleen bij een echte verandering, met hysterese en
  een minimale tijd tussen schrijfacties (bijvoorbeeld één keer per 15 min). Vraag Innova ook of
  de registers in RAM of EEPROM staan. De climate-component schrijft om dezelfde reden alleen
  waarden die echt veranderen.
- Innova zet STAT bit 12 ("master-time-out") als de master **300 s** stil is. Houd het
  pollinterval daarom ruim onder die grens. Met 10 s is dat geen probleem.
