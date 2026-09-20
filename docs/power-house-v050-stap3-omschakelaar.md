# Stap 3: de omschakelaar fork / v0.50

**Gebouwd.** `Power House engine` staat standaard op **v0.50**: de overgenomen
upstream-rekenkern stuurt, de fork rekent mee als schaduw. Eén klik terug naar
`fork` zet het om, en er is een automatische terugval als de v0.50-motor niet kan
rekenen.

Stap 1 (de logica) en stap 2 (de schaduw) staan beschreven in
[Power House v0.50-overname](power-house-v050-overname.md).

> **De drempels uit §8 zijn niet afgewacht.** Dit is op verzoek omgezet na één dag
> schaduwdraaien in plaats van na een week met vorst én zachte dagen. De
> beveiligingen hieronder staan er wel; wat ontbreekt is de statistiek die zegt
> dat het ook over een heel weerbereik klopt. Wie dat alsnog wil: zet de
> omschakelaar op `fork` en laat hem een week meerekenen.

Wat er bij het bouwen anders is geworden dan in dit voorstel stond, staat in
§11 — inclusief één fout die de uitwerking nog niet zag.

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
| `oq_phouse_req_w` | sensor `Power House – P_req` (blijft van de fork) |
| `oq_demand_raw` | sensor `Demand raw` (blijft van de fork) |
| `oq_demand_filtered` | sensor `Demand filtered`, en `oq_ot_slave.yaml:113` (OpenTherm-modulatie) |
| `oq_heating_demand_filtered` | verwarmingsvraag voor de rest van de keten |

En de strategie-interface, waar de **supervisory** op beslist of hij uit CM0 mag
komen. Die hoort er net zo goed bij, en dat was bij het bouwen niet meteen
duidelijk (zie §12):

| Global | Waarvoor |
|---|---|
| `oq_strategy_heat_request_active` | de warmtevraag zelf: `heating_req_raw` in `oq_supervisory_controlmode.yaml` |
| `oq_strategy_requested_power_w` | de low-load-grendel. Die laat pas los boven ongeveer het thermisch vermogen van de laagste stand (`on_w`, tot 2200 W) en valt terug onder `off_w` |
| `oq_strategy_phase_code` / `oq_strategy_phase_text` | diagnostiek |

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
   `restore_value: true`, standaard `v0.50`.
2. **`sensor.openquatt_power_house_motor`** — wie er wérkelijk stuurt, plus
   de reden als dat afwijkt van de keuze (zie terugval hieronder). Zonder deze
   sensor is een stille terugval onzichtbaar, en dat is precies het soort ding dat
   je een week later niet meer terugvindt.
3. **Publicatiestap** in de lus van de motor die eigenaar is: die schrijft de
   gedeelde globals, de ander schrijft ze niet. Zo maakt de volgorde waarin de
   twee intervallen vuren niet uit.
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

Drie regels, alle drie host-getest:

1. **Settletijd 180 s** (gelijk aan upstream's topologie-hold). In dat venster mag
   de binnenkomende motor geen draaiende HP stoppen en geen stilstaande HP
   starten. Alleen standwijzigingen binnen de al draaiende set. Het venster gaat
   alleen aan als er op het moment van de wissel werkelijk een compressor draait.
2. **Maximaal ±1 stand per ronde** in dat venster, per unit.
3. **Uitstel tijdens defrost of oliehold.** De keuze in de select geldt meteen,
   maar wordt pas doorgevoerd op de eerste ronde waarin geen van beide units in
   defrost of oliehold zit. `Power House motor` publiceert dan "omschakeling in
   behandeling".

Een beschermingsstop (low-flow, watertemperatuur, druk, persgas) gaat hier
dwars doorheen zonder dat deze begrenzing daar iets van hoeft te weten: die
grijpt verderop in de keten in, op de aanvraag die hier uitkomt. De begrenzing
hierboven kan een stand dus wel vasthouden in de *aanvraag*, maar niet in wat de
actuator uiteindelijk schrijft.

## 7. Automatische terugval

De v0.50-motor stuurt niet als:

| Voorwaarde | Wat er gebeurt |
|---|---|
| Frequentietabel van een HP onbekend | fork stuurt, reden "frequentietabel onbekend" |
| De v0.50-lus meldt zich een minuut niet | fork neemt over, reden "v0.50-lus zwijgt" |
| Power House niet actief (koelen, DHW, CM5) | niemand; dit pad ligt stil |

Terugval is stil in de regeling maar zichtbaar in de diagnostiek: de reden staat
in `Power House motor`. Zonder tabel is het Hz-model namelijk blind, en dan is
doorgaan erger dan terugvallen.

**Een onbruikbaar prestatiemodel geeft de besturing níet terug** — zie §11.

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

Voor drempel 4 en 6 bestaan de tellers inmiddels:

- `PH v0.50 schaduw - zou niets starten (min)` — hoort 0 te blijven
- `PH v0.50 schaduw - standwissels HP1/HP2 (per uur)` — ook de maat voor de
  ritmekeuze in §5

Ze blijven ook ná het omzetten werken: de vergelijking draait dan om. De
schaduwentiteiten tonen nog steeds wat v0.50 kiest, en `wijkt af` vergelijkt dat
met wat de fork zou hebben gedaan — die blijft doorrekenen in
`oq_ph_fork_request_hp1/hp2_level`.

## 9. De terugweg

Één keer de select terugzetten op `fork`. Dezelfde settletijd van §6 geldt dan
andersom. De fork-toestand is warm, dus er is geen aanloop.

Daar zit ook de reden om de opruiming (stap 4) niet aan stap 3 te plakken: zolang
beide motoren er staan, kost terugschakelen een klik. Ruim je de fork-rekenkern
op, dan kost het een flash. Dat is pas verstandig na een heel stookseizoen op
v0.50.

## 10. Wat er gebouwd is

| Bestand | Rol |
|---|---|
| `openquatt/includes/oq_power_house_engine.h` | eigenaarskeuze, terugval, waakhond en de begrenzing na een wissel; ESPHome-vrij |
| `tests/host/oq_power_house_engine_test.cpp` | host-test daarvan |
| `openquatt/oq_power_house_v050_shadow.yaml` | de select, `Power House motor`, en de publicatiestap van de v0.50-motor |
| `openquatt/oq_power_house_strategy.yaml` | schrijft alleen nog als de fork eigenaar is; eigen filtertoestand |
| `openquatt/oq_thermal_request_control.yaml` | redencodes 14 en 15 erbij |

Entiteiten:

| Entiteit | Wat |
|---|---|
| `select.openquatt_power_house_engine` | `fork` of `v0.50`, standaard v0.50, blijft over een herstart |
| `sensor.openquatt_power_house_motor` | wie er werkelijk stuurt, plus de reden als dat afwijkt |
| `PH v0.50 schaduw - startintentie` | `none` / `room_demand` / `setpoint_raise` / `room_recovery` — altijd, niet alleen bij een snelle start |
| `PH v0.50 schaduw - ondergrens` | het vermogen waarop de vraag blijft hangen tijdens room_recovery |

Host-tests dekken: geen venster bij het opstarten, terugval zonder tabel en
terug, een hold die de eigenaar juist niet verandert, een wissel zonder
draaiende compressor (geen venster), uitstel tijdens defrost, de begrenzing zelf
(geen start, geen stop, ±1 per ronde, venster loopt af), de waakhond en een
`millis()`-omslag.

Stap 4 — het opruimen van de fork-rekenkern — blijft staan waar het stond: pas na
een heel stookseizoen. Zolang beide motoren er zijn, kost terugschakelen een klik
in plaats van een flash.

## 11. Afwijkingen van dit voorstel

Drie dingen zijn bij het bouwen anders geworden. De eerste was een fout in het
voorstel.

**1. Een onbruikbaar prestatiemodel geeft de besturing niet terug.** Het voorstel
zei: `output_valid` onwaar → fork stuurt. Dat zou om de haverklap wisselen. Staan
beide units stil in hun minimale uit-tijd, dan is er geen kandidaat die kan
draaien, en dan meldt de dispatch `performance_valid = false` — precies de
toestand die na élke stop optreedt. Upstream houdt in dat geval de draaiende
standen vast (`oq_power_house_dispatch_logic.h`, de `!in.performance_valid`-tak),
en dat is het gewenste gedrag. De v0.50-motor blijft dus eigenaar en publiceert
die vastgehouden standen; `Power House motor` meldt "model op hold". Alleen een
onbekende frequentietabel geeft de besturing echt terug.

**2. Het settle-venster gaat alleen aan als er een compressor draait.** Anders
zou de eerste wissel na het opstarten — de fork stuurt tot de tabel binnen is,
daarna v0.50 — elke start drie minuten tegenhouden. Er valt niets te beschermen
als er niets draait.

**3. De entiteitsnamen houden het woord "schaduw".** Hernoemen breekt dashboards,
het HA-pakket en de historie waarin je de twee motoren juist vergelijkt. `Power
House motor` zegt wie er stuurt; de schaduwnamen slaan op de v0.50-motor,
ongeacht of hij aan het stuur zit.

Verder: de v0.50-motor publiceert de **begrensde** vraag in `oq_demand_filtered`
(na `oq_power_cap_f`), waar de fork daar de onbegrensde zet. Dat raakt de
OpenTherm-modulatie en de sensor `Demand filtered`, niet de standkeuze —
`oq_thermal_request_control.yaml` zegt zelf dat producenten de cap al toepassen
voor ze publiceren.

## 12. Wat er op de hardware misging

Twee dingen kwamen pas boven water toen de v0.50-motor voor het eerst echt
stuurde. Allebei zaten ze niet in de logica maar in de aansluiting, en allebei
waren ze in de schaduw onzichtbaar.

**1. De online-vlag stond altijd op false.** `${hp_id}_is_online` begon op false
en werd alleen gezet door de `on_online`-trigger van `modbus_controller`. Die
vuurt uitsluitend bij de overgang offline → online, en ESPHome start zelf in de
online-toestand. Werkte de bus vanaf het opstarten gewoon, dan vuurde die trigger
nooit. Niemand gebruikte die vlag — behalve de v0.50-kandidaatlogica. Gevolg:
beide units golden als "mag niet starten", geen kandidaat kon draaien,
`performance_valid` werd false en de dispatch hield de standen vast op 0. Voor
altijd. De kandidaatlogica vraagt het nu rechtstreeks aan de controller
(`get_module_offline()`).

**2. De strategie-interface bleef van de fork.** De supervisory beslist met
`oq_strategy_heat_request_active` en `oq_strategy_requested_power_w` of hij uit
CM0 mag komen. Die stonden nog op de waarden van de fork terwijl de v0.50-motor
de standen schreef. Dus: v0.50 vroeg HP2 op stand 1, en de supervisory keek naar
het vermogen van de fork, zag dat onder de low-load-grendel liggen en bleef in
standby. De warmtepomp bleef uit terwijl alle schaduwentiteiten er gezond
uitzagen.

Wat beide gevallen gemeen hebben: **de schaduw kan een aansluiting niet testen
die hij niet gebruikt.** Hij rekende jarenlang keurig P_req uit zonder ooit een
compressor te hoeven starten of een supervisory te hoeven overtuigen. Voor het
volgende stuk overname is de les: kijk niet alleen of de getallen kloppen, maar
loop de lijst af van alles wat de oude motor schrijft en zoek uit wie het leest.

Het vangnet uit §7 is hierop toegevoegd: laat de v0.50-motor een kwartier lang
beide units stilstaan terwijl de fork wil stoken en de kamer onder setpoint zit,
dan gaat de besturing terug naar de fork en blijft daar tot de gebruiker de keuze
aanraakt.
