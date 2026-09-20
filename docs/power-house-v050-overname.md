# Power House-logica uit upstream OpenQuatt v0.50.0

`openquatt/includes/v050/` bevat de beslislogica van Power House zoals Jeroen die in v0.50.0 heeft
losgetrokken uit de YAML. Alles hier is vrij van ESPHome en wordt host-getest
onder `tests/host/`.

| Bestand | Herkomst | Status |
|---|---|---|
| `control/oq_power_house_demand_logic.h` | upstream `control/` | ongewijzigd |
| `control/oq_power_house_dispatch_logic.h` | upstream `control/` | ongewijzigd |
| `control/oq_hp_candidate_logic.h` | upstream `control/` | ongewijzigd |
| `control/oq_heat_intent_logic.h` | upstream `control/` | ongewijzigd |
| `performance/hp_perf_frequency.h` | upstream `performance/` | aangepast: alleen V1/V1.5, gebruikt de eigen `hp_perf_map.h` |
| `odu/oq_odu_variant.h` | upstream `odu/oq_odu_generation.h` | uitgedund tot de enum |

**Ongewijzigd betekent ongewijzigd.** Fork-eigen gedrag (de v0.32-uitbreidingen,
standbegrenzing, de aansluiting op de ODU-frequentietabel) hoort in de adapter,
niet hier. Zo blijft een nieuwe upstream-versie naast deze bestanden te leggen.

Wat bewust NIET is overgenomen: het V2-prestatiemodel, de V2-standvertaling met
21 niveaus, de generatiedetectie, de incident manager en de externe
warmtevraag. Deze fork draait op V1.5 (R32).

Stap 1: alleen de logica, zonder gebruik — er verandert niets aan het
gedrag. Stap 2: een schaduwberekening die deze logica naast de huidige Power House
laat meerekenen en publiceert wat hij zou kiezen, zonder iets aan te sturen.

## Stap 2: schaduw

| Bestand | Rol |
|---|---|
| `openquatt/includes/oq_power_house_v050_adapter.h` | aansluiting op de fork, zonder ESPHome, host-getest |
| `openquatt/includes/oq_power_house_v050_shadow.h` | de schaduwberekening, volgt upstream's `oq_power_house_runtime.h` |
| `openquatt/oq_power_house_v050_shadow.yaml` | interval en entiteiten, groep "Power House v0.50 (schaduw)" |

### Wat de adapter aansluit

- **Frequentietabel.** Stand → Hz uit de ODU. Elke HP leest zijn tabel stil in:
  20 s na opstart en daarna elke 15 min, niet tijdens een EEPROM-dump. Na
  "tabel ophalen" of schrijven wordt de opgeslagen tabel meteen bijgewerkt.
  Zolang de tabel onbekend is, start de v0.50-dispatch niets nieuws (zoals upstream).
- **Toegestane standen.** De bestaande `Day max level` / `Silent max level` en
  `Excluded level A/B`. Er zijn geen nieuwe Hz-instellingen.
- **Kandidaten** (in plaats van de incident manager):
  - *moet stoppen:* low-flow-fout of watertemperatuur-trip
  - *mag starten:* online en geen opstartblokkade
  - *klaar:* minimale uit-tijd voorbij én startlimiet (6/uur) vrij
  - *verdacht:* Modbus offline
- **De v0.32-uitbreidingen** rekenen met dezelfde formules mee.
  `oq_power_house_v050_adapter_test.cpp` vergelijkt een uur lang, elke 10 s, met
  een letterlijke naschrijving van de YAML. Vermogen, vraag f en comfortgeheugen
  moeten exact gelijk zijn.

| Uitbreiding | Waar in de berekening |
|---|---|
| #2 zon/interne winst | feedforward min correctie |
| #3 voorverwarmen | ondergrens op het ruwe vermogen |
| smart #1 tarief, #2 PV-boost | opgeteld na de begrenzer, tot 1,2× nominaal |
| smart #3 raam open | vermogen 0 |
| #4 vorstzone-derating | thermisch vermogen per kandidaat |
| #5 effectieve aanvoer | aanvoertemperatuur voor het prestatiemodel |

**Tarief en PV stapelen niet op.** De begrenzer onthoudt het vermogen *vóór*
tarief en PV. Onthoudt hij het erna, dan telt de boost elke cyclus opnieuw mee, en
zodra de boost groter is dan wat de begrenzer per cyclus laat zakken, loopt het
vermogen op tot 1,2× nominaal, los van de warmtevraag:

| Ritme / profiel | Zakken per cyclus | Stapelt op bij boost boven |
|---|---|---|
| 60 s, Balanced (fall 3 min) | 2340 W | 2340 W |
| 60 s, Calm (fall 5 min) | 1404 W | 1404 W, dus al met PV-max 2000 W of tarief-standaard 1500 W |
| 10 s (v0.50), Balanced | 390 W | 390 W |

Een PV-boost van 900 W is nu netto 900 W extra, niet 8424 W na 20 minuten
(`test_boost_does_not_ratchet`).

Dit is tijdens de overname in de adapter gevonden en daarna ook in de huidige
Power House gerepareerd (`oq_power_house_strategy.yaml`). Beide motoren doen op
dit punt hetzelfde; de schaduwvergelijking laat er dus geen verschil meer door
zien. Tarief en PV staan standaard uit, dus wie ze nooit heeft aangezet merkt van
de reparatie niets.

De toestand van zon-EMA, tariefverschuiving, PV-boost en raamdetectie wordt
**gelezen**, niet bijgewerkt. De huidige Power House blijft daar eigenaar van.
UA-leren en de Kp-observer zijn diagnostiek en blijven in de huidige Power House.

### Verschillen die je in de schaduw ziet (dus geen fout)

- Rekenritme 10 s in plaats van 60 s.
- Geen extra stappenfilter (`Demand filter ramp up`).
- Prestatiemodel op de werkelijke Hz uit de ODU-tabel.
- Een HP in minimale uit-tijd of startlimiet wordt niet gekozen.
- Snelle eerste start op de laagste haalbare stand (kamervraag bevestigd na 10 s, of
  setpoint ≥ 0,2 K verhoogd).

Eén fork-regel is niet meegenomen, omdat upstream hem niet heeft: tijdens defrost
bij één draaiende HP houdt de fork de huidige eigenaar extra vast als de beste
single-kandidaat op de andere HP valt. Upstream dekt dat met de defrost-hold op
eigenaarwissels.

### Uitlezen

- `PH v0.50 schaduw - wijkt af` en `- verschil (huidig -> v0.50)`: moment en inhoud.
- `PH v0.50 schaduw - afwijkend (min)`: opgeteld sinds de herstart.
- `- reden`, `- snelle start`, `- verwacht thermisch vermogen`, `- capaciteit`.
- `PH v0.50 - HPx frequentietabel verwarmen`: welke Hz het model gebruikt.

Twee cijfers zijn geen verschil maar een oordeel:

| Entiteit | Wat het zegt | Wat je wil zien |
|---|---|---|
| `- zou niets starten (min)` | v0.50 zou beide units stil laten staan terwijl er wél gestookt wordt en de kamer onder setpoint zit | **0**. Dit is de enige afwijking die een koud huis oplevert; de eerste keer dat het gebeurt staat het ook in de log, met de reden erbij |
| `- standwissels HP1/HP2 (per uur)` | hoe vaak de v0.50-keuze de stand zou verzetten, over een voortschrijdend uur | onder ongeveer 6 per uur per unit. Daarboven is het rekenritme van 10 s te snel — in Power House-modus staat de slew-begrenzing van ±1 stand downstream namelijk uit |

Het tweede cijfer beantwoordt een van de twee open vragen voor stap 3 met data in
plaats van met een vermoeden. Zolang de frequentietabel onbekend is, blijven beide
op 0 respectievelijk leeg: een keuze van 0 zegt dan niets over de logica.

Begin altijd bij de frequentietabel. Staat daar binnen een minuut na opstart niet
`30, 39, 49, 55, 61, 67, 72, 79, 85, 90` Hz, dan rekent het Hz-model op een andere
tabel dan de fabriekstabel en zegt het verschil weinig.

Beide dashboards hebben hiervoor de sectie **Power House v0.50 (schaduw)**: in V1
op de tab Diagnostiek, in V2 op Uitzoeken. Met twee grafieken ernaast — gevraagd
vermogen en standkeuze, huidig tegen v0.50.

De firmwareteller begint bij elke herstart opnieuw. Voor een dag- en weekcijfer
staat er een optioneel Home Assistant-pakket bij:
`docs/dashboard/openquatt_ha_power_house_v050_package.yaml`. Dat levert
`sensor.ph_v050_afwijkend_vandaag_min`, `..._aandeel_vandaag`, `..._afwijkend_7d`,
`..._p_req_verschil_24u` en `..._standverschil`.

Een week met zowel een vorstnacht als een zachte dag is het ijkpunt: juist de
vorstzone-factor en de effectieve aanvoertemperatuur zijn de plekken waar de twee
modellen uiteen kunnen lopen.

## Stap 3: de omschakelaar

`Power House engine` bepaalt welke motor de aanvraag schrijft, en staat standaard
op **v0.50**. Beide motoren blijven altijd rekenen — dat moet ook, want de
v0.32-uitbreidingen worden in de fork-lus bijgewerkt en door de adapter gelezen.

De hele uitwerking, inclusief de beveiliging bij een draaiende compressor, de
automatische terugval en wat er bij het bouwen anders werd dan gepland, staat in
[stap 3: de omschakelaar](power-house-v050-stap3-omschakelaar.md).
