# Stap 3: de omschakelaar fork / v0.50 (voorstel)

Dit is een **voorstel**, nog geen firmware. Stap 1 (de logica) en stap 2 (de
schaduw) staan er; zie [Power House v0.50-overname](power-house-v050-overname.md).
Stap 3 geeft de v0.50-rekenkern de besturing, met een knop om terug te gaan.

Lees dit voor je iets bouwt: de keuzes hieronder zijn de keuzes, en de drempels
onderaan bepalen wanneer het verstandig is om om te zetten.

## 1. Waar de naad zit

De huidige Power House schrijft zijn besluit in vier globals. Alles daarachter —
minimale looptijd, uitgesloten standen, de startlimiet, de beschermingen, de
actuator — is gedeeld en staat los van welke motor het besluit nam.

| Global | Wat erin staat | Wie leest |
|---|---|---|
| `oq_ph_request_hp1_level` | stand HP1 | `oq_thermal_request_control.yaml:1219` |
| `oq_ph_request_hp2_level` | stand HP2 | idem |
| `oq_ph_request_owner_hp` | 0/1/2 | idem |
| `oq_ph_request_reason_code` | reden, 0–14 | idem, wordt tekst in `Request Reason` |

Daarnaast vier waarden die de rest van het systeem voedt:

| Global | Waarvoor |
|---|---|
| `oq_phouse_req_w` | sensor `Power House – P_req` |
| `oq_demand_raw` | sensor `Demand raw` |
| `oq_demand_filtered` | sensor `Demand filtered`, en `oq_ot_slave.yaml:113` (OpenTherm-modulatie) |
| `oq_heating_demand_filtered` | verwarmingsvraag voor de rest van de keten |

**Dat is de hele naad.** De omschakelaar hoeft niets anders te raken.

## 2. Het ontwerp: beide motoren rekenen, één schrijft

Vandaag rekent de schaduw al continu mee. Dat blijft zo; het enige wat de
omschakelaar doet, is bepalen wie de acht globals hierboven mag vullen.

Waarom niet "de andere motor uitzetten":

- **De v0.32-uitbreidingen wonen in de fork-lus.** Zon-EMA, tariefslew, PV-slew
  en raamdetectie worden daar bijgewerkt; de adapter *leest* ze. Zet je die lus
  stil, dan bevriezen ze.
- **Toestand blijft warm.** Comfortgeheugen, vermogensbegrenzer, topologie-hold:
  beide motoren houden die zelf bij. Terugschakelen is daardoor onmiddellijk en
  zonder aanloop.
- **De vergelijking blijft werken, in beide richtingen.** Na het omzetten is de
  fork de schaduw, en zie je wat díe zou hebben gedaan.

Kosten: geen. Beide lussen draaien nu al.

### Eén ding moet wel uit elkaar

De fork-lus leest zijn eigen filtertoestand terug uit `oq_demand_filtered`
(`oq_power_house_strategy.yaml:1598`). Zodra de v0.50-motor die global vult, leest
de fork de volgende ronde een waarde die niet van hem is. Dus: de fork krijgt een
eigen `oq_ph_fork_demand_filtered` voor zijn filtertoestand, en `oq_demand_filtered`
wordt alleen nog gevuld door de publicatiestap.

## 3. Wat er wisselt en wat niet

**Wisselt met de omschakelaar:**

| | fork | v0.50 |
|---|---|---|
| Vraagberekening | elke 60 s | elke 10 s (instelbaar) |
| Stappenfilter | `Demand filter ramp up` | geen |
| Prestatiemodel | per stand | per Hz uit de ODU-tabel |
| Standkeuze | eigen optimizer | upstream dispatch |
| Startgedrag | via de vraag | snelle eerste start op de laagste haalbare stand |
| Kandidaatregels | impliciet | expliciet: minimale uit-tijd en startlimiet sluiten een HP uit |

**Wisselt niet — dit staat achter de naad en geldt altijd:**

minimale looptijd (300 s), de startlimiet van 6 per voortschrijdend uur,
uitgesloten standen A/B, `Day max level` / `Silent max level`, de externe
vermogenscap (`oq_power_cap_f`, Energy OS), druk-, aanvoer- en
persgasbeveiliging, de low-flow-bewaking, de watertemperatuur-trip, DHW en
koeling (die lopen sowieso langs Power House heen), en de actuator zelf.

## 4. Wat ervoor nodig is

1. **`select.openquatt_power_house_engine`** — opties `fork` en `v0.50`,
   `restore_value: true`, standaard `fork`.
2. **`sensor.openquatt_power_house_engine_active`** — wie er wérkelijk stuurt, plus
   de reden als dat afwijkt van de keuze (zie terugval hieronder). Zonder deze
   sensor is een stille terugval onzichtbaar, en dat is precies het soort ding dat
   je een week later niet meer terugvindt.
3. **Publicatiestap** aan het eind van de verwarmingslus: kopieert de acht globals
   uit de winnende motor. Eén plek, één `if`.
4. **Eigen filtertoestand voor de fork** (§2).
5. **Redencodes.** Gelukkig bijna gratis: upstream's `Reason` 0–14 in
   `oq_power_house_dispatch_logic.h:18` is **identiek** aan de codes die
   `oq_thermal_request_control.yaml` al vertaalt. Alleen code 15
   (`oil_return_hold`) is nieuw en moet in die `switch` erbij.
6. **De omschakelbeveiliging** uit §6, als eigen header met host-tests.

Wat er níet bij hoeft: nieuwe instellingen voor Hz, een tweede stel
comfortinstellingen, of aanpassingen aan de actuator.

## 5. De twee open beslissingen

### Ritme: 10 s of 60 s

**Voorstel: 10 s (upstream-default), met een getalsinstelling om het te verhogen.**

Wat 10 s opvangt zit al in de keten: topologie-hold 180 s, defrost-hold 180 s,
oliehold 120 s, minimale looptijd 300 s, startlimiet 6/uur.

Het echte risico is een ander: **in Power House-modus staat de expliciete
slew-begrenzing van ±1 stand uit.** `oq_thermal_request_control.yaml:1321` past die
alleen toe als `!is_power_house` — de strategie hoort het zelf te doen. Op 60 s
valt dat niet op; op 10 s kan een stand daardoor zes keer per minuut bewegen.

**Zo beslis je het met data in plaats van met een vermoeden:** tel in de schaduw
de standwissels per uur (zie §8). Blijft dat onder ongeveer 6 per uur per unit,
dan is 10 s prima. Zit het daarboven, dan 30 s, of de ±1-begrenzing ook in
Power House-modus aanzetten.

### `Demand filter ramp up`

**Voorstel: niet toepassen in v0.50-modus, wel behouden voor fork-modus.**

Upstream heeft dat filter niet, en het werk dat het doet — voorkomen dat de vraag
in één sprong omhoog gaat — wordt in v0.50 gedaan door de vermogensbegrenzer
(rise/fall time, in W/s) en door de holds in de dispatch. Twee filters op elkaar
maken het gedrag alleen maar moeilijker te verklaren.

Het is geen weggooien: in fork-modus blijft de instelling doen wat hij doet. Valt
de standwisseltelling uit §8 tegen, dan is dit filter de eerste kandidaat om
alsnog aan te zetten.

## 6. Veilige overgang bij een draaiende compressor

Het moment van omschakelen is het enige echt nieuwe risico. De nieuwe motor heeft
een warme toestand, maar kan meteen een andere stand willen — en in Power
House-modus zit er geen ±1-begrenzing tussen.

Voorstel, drie regels, alle drie host-testbaar:

1. **Settletijd 180 s** (gelijk aan upstream's topologie-hold). In dat venster mag
   de binnenkomende motor geen draaiende HP stoppen en geen stilstaande HP
   starten. Alleen standwijzigingen binnen de al draaiende set.
2. **Maximaal ±1 stand per ronde** in dat venster, per unit.
3. **Uitstel tijdens defrost of oliehold.** De keuze in de select geldt meteen,
   maar wordt pas doorgevoerd op de eerste ronde waarin geen van beide units in
   defrost of oliehold zit. `Power House engine active` publiceert dan
   "omschakeling in behandeling".

Uitzondering op alle drie: een beschermingsstop (low-flow, watertemperatuur,
druk, persgas) gaat altijd voor. Die zit achter de naad en hoeft dus niets te
weten van de omschakeling.

## 7. Automatische terugval

De v0.50-motor stuurt niet als:

| Voorwaarde | Wat er gebeurt |
|---|---|
| Frequentietabel van een HP onbekend | fork stuurt, reden "tabel onbekend" |
| `output_valid` onwaar (ongeldige invoer) | fork stuurt, reden "ongeldige invoer" |
| Power House niet actief (koelen, DHW, CM5) | niemand; dit pad ligt stil |

Terugval is stil in de regeling maar luid in de diagnostiek: de reden staat in
`Power House engine active` en gaat naar de log. Zonder tabel is het Hz-model
namelijk blind, en dan is doorgaan erger dan terugvallen.

Na een herstart geldt de keuze weer (`restore_value: true`). De tabel wordt
ongeveer 20 s na opstart gelezen; tot dat moment stuurt de fork. Dat is meteen het
antwoord op "wat doet hij na een stroomstoring om drie uur 's nachts": gewoon
verwarmen, met de oude motor, tot de tabel binnen is.

## 8. Wanneer omzetten — go/no-go

Niet omzetten op een gevoel. Dit zijn de drempels; ze komen uit de kaart en de
sensoren die er nu al zijn (zie
[Power House v0.50-overname §Uitlezen](power-house-v050-overname.md)).

| # | Drempel | Waarom |
|---|---|---|
| 1 | ≥ 7 aaneengesloten dagen schaduwdata, met minstens één etmaal gemiddeld onder 2 °C en één boven 10 °C | de vorstzone-factor en de effectieve aanvoer zijn juist aan de randen anders |
| 2 | Frequentietabel beide units onafgebroken bekend | anders vergelijk je met een blind model |
| 3 | `sensor.ph_v050_p_req_verschil_24u` ≤ 300 W (≈ 4 % van 7020 W) op 5 van de 7 dagen | de vraagberekening hoort gelijk te lopen; alleen het ritme mag schelen |
| 4 | **Nul** monsters waarin v0.50 `no_candidate` of stand 0 kiest terwijl de huidige motor wél stookt en de kamer onder setpoint zit | dit is de enige afwijking die een koud huis oplevert |
| 5 | Nul keuzes boven `Day max level` / `Silent max level` of op een uitgesloten stand | de standregels moeten door de adapter heen komen |
| 6 | Elk verschil valt in een van de vijf bekende categorieën (ritme, filter, Hz-model, minimale uit-tijd/startlimiet, snelle start) | een verschil dat je niet kunt verklaren, is een bug tot het tegendeel blijkt |

Drempel 4 en 6 zijn met de huidige entiteiten alleen met de hand te controleren.
Twee kleine tellers maken dat een getal — die horen in stap 3a thuis, of eerder
als je de week nog moet gaan draaien:

- `PH v0.50 schaduw - zou niets starten terwijl er gestookt wordt (min)`
- `PH v0.50 schaduw - standwissels per uur` (per unit), ook de maat voor de
  ritmekeuze in §5

## 9. De terugweg

Één keer de select terugzetten op `fork`. Dezelfde settletijd van §6 geldt dan
andersom. De fork-toestand is warm, dus er is geen aanloop.

Daar zit ook de reden om de opruiming (stap 4) niet aan stap 3 te plakken: zolang
beide motoren er staan, kost terugschakelen een klik. Ruim je de fork-rekenkern
op, dan kost het een flash. Dat is pas verstandig na een heel stookseizoen op
v0.50.

## 10. Opdeling en tests

| PR | Inhoud | Risico |
|---|---|---|
| 3a | select, engine-active-sensor, publicatiestap, eigen filtertoestand, redencode 15, omschakelbeveiliging, de twee tellers uit §8 | standaard `fork`, dus geen gedragswijziging tot je zelf omzet |
| 3b | eventueel de standaard omzetten naar `v0.50` | pas na de drempels van §8 |
| 4 | opruimen van de fork-rekenkern | pas na een stookseizoen |

Host-tests bij 3a:

- omschakelbeveiliging: geen start en geen stop binnen de settletijd, ±1 per
  ronde, venster loopt af, beschermingsstop gaat er dwars doorheen
- uitstel tijdens defrost en oliehold, en dat de omschakeling daarna alsnog
  doorgaat
- terugval bij onbekende tabel en bij ongeldige invoer, in beide richtingen
- redencode 15 vertaalt naar `oil_return_hold`

Alles in `tests/host/`, zoals de rest. Lokaal is er geen compiler; CI is de
bouwcontrole.
