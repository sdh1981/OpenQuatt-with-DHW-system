# DHW: werking en instellingen

Deze pagina beschrijft hoe de warmwaterregeling (DHW) van deze fork werkt en wat elke instelling doet, zoals de firmware het nu uitvoert. De regeling stuurt een 3-wegklep, de warmtepomp(en) en een elektrisch element van 3 kW aan via een toestandsmachine (`openquatt/includes/oq_dhw_controller_logic.h`). De aansturing daaromheen staat in `openquatt/oq_boiler_control.yaml`.

Namen tussen backticks zijn de entiteitnamen zoals ze in Home Assistant en de web-UI staan.

Aanvullend:

- [DHW smart features v0.32](dhw-smart-features-v0.32.md): tarief- en PV-verschuiving van de startdrempel, gebruikspatroon, standby-verlies, time-to-ready.
- [DHW-rendement en tapdetectie v0.54](dhw-rendement-en-tapdetectie-v0.54.md): cyclus-COP en tapdetectie.
- [Hardware Duo + DHW (LilyGO)](hardware-dhw-lilygo.md): bekabeling, GPIO en sensorkanalen.

---

## Op hoofdlijnen

Er zijn vijf soorten cycli. Ze lopen allemaal via dezelfde toestanden:

| Cyclus | Start | Wat er draait |
|---|---|---|
| **Gewone cyclus** | tanktop onder de startdrempel | warmtepomp, daarna eventueel element (natraject) |
| **Legionella** | wekelijks, of handmatig geforceerd | warmtepomp en element samen, daarna element met hold |
| **Snelboost** | schakelaar `DHW boost now` | beide warmtepompen en element tegelijk |
| **Solar boost** | handmatig, HA, goedkoop tarief of PV-export | element, optioneel met warmtepomp |
| **Natraject** | na de HP-fase, als de tank nog onder het boostdoel zit | element |

```
IDLE_CV
  ├─ legionella verschuldigd of geforceerd ──► DHW_PREPARE ──► LEGIONELLA ─────────────► IDLE_CV
  ├─ DHW boost now aan                      ──► DHW_PREPARE ──► DHW_BOOST (HP's + element) ► IDLE_CV
  ├─ tanktop < startdrempel                 ──► DHW_PREPARE ──► DHW_HEAT_PUMP
  │                                                               ├─ natraject aan  ──► DHW_BOOST ──► IDLE_CV
  │                                                               └─ natraject uit  ───────────────► IDLE_CV
  └─ solar boost                            ──► DHW_PREPARE ──► DHW_BOOST ──────────────► IDLE_CV

Elke toestand ──► FAULT (alleen te verlaten met "DHW clear fault")
```

Zijn er meerdere redenen tegelijk, dan geldt deze volgorde: legionella, snelboost, gewone cyclus, solar boost.

De regeling rekent elke 2 s (`oq_dhw_loop_s`).

---

## Sensoren en bronnen

| Meting | Eerste bron | Tweede bron | Laatste terugval |
|---|---|---|---|
| `DHW tank top` | CWT kanaal 1 (PT1000) | HA-proxy `sensor.openquatt_ext_dhw_tank_top` | `DHW source tank top` (50 °C) |
| `DHW tank bottom` | CWT kanaal 2 | HA-proxy `..._tank_bottom` | `DHW source tank bottom` (45 °C) |
| `DHW coil in` | CWT kanaal 3 | HA-proxy `..._coil_in` | `DHW source coil in` (35 °C) |
| `DHW coil out` | CWT kanaal 4 | HA-proxy `..._coil_out` | `DHW source coil out` (30 °C) |

> **Let op de laatste terugval.** Vallen de PT1000-module én de HA-proxy weg, dan krijgt de regeling een vaste, plausibel ogende waarde. Dat levert geen sensorfout op. Voor de tankbodem betekent dit: 45 °C haalt nooit de stopgrens van 52 °C, dus de HP-fase loopt door tot de maximale looptijd van 180 min. Controleer daarom bij twijfel de CWT-kanalen zelf.

**Plausibiliteit.** De regeling gaat naar `SENSOR_IMPLAUSIBLE` (zie [Fouten](#fouten)) bij:
- tanktop ontbreekt of ligt buiten −10…85 °C;
- tankbodem, spiraal-in of spiraal-uit ligt buiten dat bereik (ontbreken mag);
- bodem meer dan 12 K warmer dan de top;
- spiraal-in en -uit meer dan 40 K uit elkaar.

**Klepstand.** Die komt van het hulpcontact (`DHW valve aux (DHW position)`); het contact sluit in DHW-stand.

---

## De gewone cyclus

### 1. Start

Een nieuwe cyclus start als de tanktop onder de **effectieve startdrempel** zakt en niets blokkeert.

#### `DHW start top`

- Standaard **46 °C**, bereik 38–50 °C.

**Effectieve startdrempel** = `DHW start top` + de verschuivingen uit de smart features: tarief, PV en gebruikspatroon (zie [DHW smart features](dhw-smart-features-v0.32.md)). De drempel blijft altijd tussen 30 °C en `DHW HP stop top` − 1 K.

**Wat een start blokkeert:**
- een gelatchte fout, `DHW lockout`, of een HP-fout (zie [Fouten](#fouten));
- buiten het [tijdvenster](#tijdvenster), of in het [duurste tariefvenster](#dure-uren-vermijden);
- de [minimale rusttijd](#dhw-minimum-rest) sinds de vorige cyclus.

### 2. Klep

De klep gaat naar DHW. Na 4 s moet het hulpcontact de DHW-stand bevestigen. Lukt dat niet binnen 20 s, dan volgt één nieuwe poging. Mislukt ook die, dan volgt `VALVE_STUCK_CV`. Pas met een bevestigde klep gaat de warmtepomp aan.

### 3. HP-fase

De warmtepomp laadt met aanvoerdoel `DHW HP target flow`.

#### `DHW HP target flow`

- Standaard **55 °C**, bereik 48–60 °C.
- Geldt in de HP-fase, in de HP-fase van legionella en wanneer een warmtepomp meedraait in een boost.

**Stoppen.** De HP-fase stopt zodra de **tankbodem 52 °C** bereikt. Die grens staat vast in de firmware, zodat elke cyclus de hele tank doorwarmt. Alleen als de bodemsensor ontbreekt of onplausibel is, geldt `DHW HP stop top` op de tanktop. Na **180 min** stopt de fase hoe dan ook (vast, geen fout).

#### `DHW HP stop top`

- Standaard **49 °C**, bereik 42–56 °C.
- Stopgrens op de top; alleen gebruikt zonder bodemsensor.
- Ook de bovengrens van de effectieve startdrempel (−1 K) en het doel van `DHW estimated time to ready`.

> **Instellingen zonder effect.** `DHW stop on tank bottom` en `DHW HP stop tank bottom` bestaan nog om oude configuraties niet te breken, maar de firmware negeert ze. De bodemstop op 52 °C is altijd actief.

### 4. Compressorniveau

Het niveau dat de warmtepomp krijgt, wordt in drie stappen bepaald.

1. **`DHW HP level`** (standaard **4**, bereik 1–10): het gevraagde niveau.
2. **`DHW coil-in level mapping`** (standaard **uit**): knijpt het niveau af op de spiraalintrede.

   | Spiraal-in stijgt boven | Niveau |
   |---|---|
   | `DHW coil map T1 (4→3)` (40 °C) | 4 → 3 |
   | `DHW coil map T2 (3→2)` (44 °C) | 3 → 2 |
   | `DHW coil map T3 (2→1)` (48 °C) | 2 → 1 |

   - Hysterese: `DHW coil map hysteresis` (0,5 K).
   - Nooit onder 1 en nooit boven `DHW HP level`.
   - Het resultaat staat in `DHW coil mapped level`.
3. **`DHW soft start step time`** (standaard **3 min**, 0 = uit): de compressor begint op niveau 1 en mag elke stap één niveau hoger, tot wat gevraagd wordt.

Hoe dat niveau over de units wordt verdeeld, staat bij [Warmtepompen tijdens DHW](#warmtepompen-tijdens-dhw-duo).

### 5. Natraject met het element

#### `DHW boost after HP`

- Standaard **aan**.

Na de HP-fase gaat het element aan als de tanktop nog onder `DHW boost target` zit. Het element stopt op dat doel. Het natraject duurt hooguit **90 min**. Haalt het het doel niet, dan gaat de regeling terug naar rust **zonder fout**, en telt `DHW boost timeouts` op.

#### `DHW boost target`

- Standaard **56 °C**, bereik 55–58 °C.
- Doel van het natraject en van de solar boost.

### 6. Minimale rust

#### `DHW minimum rest`

- Standaard **1200 s**, bereik 0–3600 s (0 = uit).

Wachttijd na het einde van een cyclus voordat een nieuwe **gewone** cyclus mag starten. Legionella, snelboost en solar boost vallen er niet onder.

---

## Warmtepompen tijdens DHW (Duo)

### Duo of single

#### `DHW single HP mode`

- Standaard **uit**.

**Uit.** Beide units draaien op hetzelfde niveau. Reden in `Request Reason`: `dhw_duo`.

**Aan.** Eén unit, de **lead**, maakt het warme water. Reden: `dhw_single_hp1` of `dhw_single_hp2`, en met assist `dhw_single_hp1_assist` of `dhw_single_hp2_assist`.

- **Lead kiezen.** Bij de start van een cyclus, in deze volgorde:
  1. Heeft één unit een harde storing (`OT fault`), dan wordt de andere lead.
  2. Knijpt één unit zichzelf af (`freq limited`), dan wordt de andere lead.
  3. Anders de unit met de minste draaiuren.
- **Lead vastzetten.** De lead blijft staan tot het einde van de cyclus. Alleen een harde storing op de lead zet de lead over. Een frequentiebegrenzing doet dat bewust niet: bij DHW-temperaturen knijpen beide units regelmatig af, en wisselen zou elke keer een extra start en stop kosten.
- **Uitlezen:** `DHW single HP lead`.
- **Snelboost:** single-HP mode geldt niet; daar draaien altijd beide units.

#### `DHW single HP level bump`

- Standaard **0**, bereik 0–3.

Telt op bij het niveau van de lead, en valt onder de zachte aanloop. De bump geeft extra capaciteit zonder dat er gemeten is dat die nodig is. Voor capaciteit op basis van meting is de assist hieronder bedoeld.

### Tweede-HP assist

Alleen actief als `DHW single HP mode` aan staat, en alleen in de gewone HP-fase; niet tijdens legionella. De andere unit springt stapsgewijs bij als de lead het alleen niet redt.

#### `DHW second HP assist`

- Standaard **aan**.

#### Wanneer de assist inschakelt

"Te kort" betekent dat beide gelden:
- de lead krijgt al zijn volle `DHW HP level`, dus de zachte aanloop en de coil-mapping knijpen niet meer;
- de tanktop stijgt langzamer dan `DHW assist min tank rise` (**0,10 K/min**), gemeten per 2 minuten.

Houdt dat **10 min** aan, dan start de tweede unit op **niveau 1**.

#### Opbouwen en afbouwen

| Situatie | Gedrag |
|---|---|
| Nog steeds te kort | elke 10 min één niveau erbij, tot `DHW assist max level` (**3**) |
| Tank stijgt weer goed | elke 10 min één niveau eraf |
| Op niveau 1 en niet meer nodig | eruit na minimaal 15 min draaien, daarna 15 min wachttijd |
| Tankbodem ≥ `DHW assist tank bottom stop` (**48 °C**) | meteen eruit, zonder wachttijd; de laatste graden op één compressor |
| Einde HP-fase | eruit, wachttijd gewist |

#### Bewaking

De assist gaat meteen helemaal uit, met 15 min wachttijd, als:
- het persgas van een van beide units boven `DHW assist max discharge temp` (**90 °C**) komt, of
- de water-uit van de assisterende unit boven `DHW assist max water out` (**57 °C**) komt.

**Vrijgeven.** De blokkade vervalt pas als het persgas 5 K en het water 2 K onder de grens zit.

**Vroeger terug na een watertrip.** Was alleen het water te warm, dan mag de assist ook vóór het einde van de wachttijd terug, zodra de water-uit onder `DHW assist water release temp` zakt. Die staat op **50 °C** en is begrensd op maximaal de watergrens − 2 K.

Deze grenzen bepalen alleen of de assist mag draaien. Het afknijpen van niveaus op aanvoertemperatuur blijft bij de [aanvoerbeveiliging](supply-temp-protection-v0.32.md).

**Uitlezen:** `DHW second HP assist status`. Mogelijke waarden: `Standby - lead haalt het alleen`, `Actief - 2e HP op level N`, `Geblokkeerd - persgas of water-uit te hoog`, `Wachttijd na uitschakelen`, `Uit - tank bijna klaar, staart op 1 compressor` en `Uit tijdens legionella`.

> Het criterium "te kort" wordt ook gemeten terwijl de assist draait. Helpt hij goed, dan bouwt hij zichzelf weer af. Het resultaat kan een ritme zijn van ongeveer 15–20 min aan en 25 min uit, ruwweg anderhalve start per uur op de assisterende unit. Houd bij twijfel de starts van die unit in de gaten.

### Wat verder altijd geldt

DHW is voor de rest van de regeling gewone warmtevraag. Supervisory zet de installatie op **CM4**, en alle compressorbeveiligingen blijven gelden:

- **Starts:** minimale uit-tijd per compressor (240 s), minimale looptijd (≥ 300 s), en maximaal **6 starts per uur** per compressor (zie [Instellingen en meetwaarden](instellingen-en-meetwaarden.md)).
- **Flow:** de systeem-lowflowbewaking (250 L/h, 60 s), ook tijdens de afbouw van een compressor (zie [Regelgedrag](regelgedrag-van-openquatt.md)).
- **Temperatuur en druk:** [aanvoer](supply-temp-protection-v0.32.md), [persgas](discharge-protection-v0.52.md) en [druk](pressure-protection-v0.32.md).
- **Pompstoringsbit:** register 2121 bit 13 ("DC water pump failure") telt niet als storing. Sommige pompen zetten dat bit in stilstand, en het zou anders de lead laten wisselen.

---

## Element en boosts

### Solar boost

Een boost met alleen het element, vanuit rust. Doel: `DHW boost target`.

**Blokkades.** Een solar boost start niet bij een fout, lockout, HP-fout, buiten het tijdvenster of in het dure tariefvenster. De minimale rusttijd geldt niet.

| Trigger | Entiteit | Onder `DHW auto boost enable`? |
|---|---|---|
| Handmatig | `DHW source solar boost` | nee |
| HA-proxy | `binary_sensor.openquatt_ext_dhw_solar_boost` | ja |
| Goedkoop tarief | `DHW solar boost auto` + `DHW solar tariff threshold` (**0,02 EUR/kWh**) | ja |
| PV-export | `DHW PV self-consumption enable` + `DHW PV boost export threshold` (**2700 W**) | ja |

#### `DHW auto boost enable`

- Standaard **uit**.

Hoofdschakelaar voor de drie automatische triggers. Hij staat bewust uit: alle drie pieken ze 's middags, en dan ging het element ongevraagd aan.

#### `DHW boost reden`

Waarom de lopende boost draait, vastgelegd bij de start. Mogelijke waarden: `Handmatige snelboost`, `Natraject na HP-fase - element maakt af`, `Solar boost - bron-schakelaar (handmatig)`, `Solar boost - HA-proxy`, `Solar boost - goedkoop tarief`, `Solar boost - PV-export` en `Geen boost actief`.

### Warmtepomp mee in de boost

#### `DHW boost HP assist`

- Standaard **uit**.

Laat een warmtepomp meedraaien in een solar boost of natraject, maar alleen als de tankbodem bij de start van de boost onder `DHW boost HP assist bottom threshold` (**35 °C**) zit. Die keuze wordt bij de start vastgelegd.

De warmtepomp stopt bij tanktop ≥ `DHW boost HP assist stop top` (**52 °C**); het element maakt af tot `DHW boost target`.

### Snelboost

#### `DHW boost now`

- Schakelaar.

Beide warmtepompen en het element tegelijk, voor snel herstel van de tank.

**Starten**
- Gaat vóór de gewone cyclus.
- Negeert het tijdvenster en het dure-urenvenster.
- Start niet bij een fout, lockout of HP-fout, of als de tanktop al op het snelboostdoel zit.
- Single-HP mode geldt niet.

**Tijdens de boost**
- **Warmtepompen stoppen** bij tanktop ≥ `DHW boost now HP stop` (**55 °C**), of zodra bij een van beide units het persgas boven 90 °C of de water-uit boven 57 °C komt (dezelfde grenzen als de assist). Binnen dezelfde boost starten ze niet opnieuw.
- **Element** gaat door tot `DHW boost now target` (**60 °C**).

**Einde**
- **Afbreken:** zet `DHW boost now` uit, ook tijdens het wachten op de klep.
- De schakelaar gaat vanzelf uit zodra de snelboost klaar is.
- Hooguit **90 min**; een timeout is geen fout (`DHW boost timeouts`).

---

## Legionella

### Planning

- **Interval:** elke 7 dagen na de laatste geslaagde run. Het tijdstip wordt persistent bewaard en overleeft een herstart.
- **Nog nooit gedraaid:** eerste run 30 min na opstart, zodra de klok (NTP) geldig is.
- **Uitlezen:** `DHW legionella laatste run` en `DHW legionella volgende run`.

### Verloop

1. **Warmtepomp en element samen**
   - De warmtepomp stopt zodra de **tankbodem** `DHW legionella HP handover temp` (**53 °C**) haalt,
   - of zodra de **tanktop** `DHW legionella HP top ceiling` (**55 °C**) haalt,
   - of na 180 min.

   Het plafond op de top is een beveiliging. Het element verwarmt de top mee, waardoor de condensatiedruk van de warmtepomp anders oploopt tot de ODU afslaat. Bij R32 is die marge klein.
2. **Element alleen** tot `DHW legionella target` (**68 °C**, bereik 60–75 °C; eis van de Inventum-boiler).
3. **Hold van 15 min** op de tanktop. Een dip tot 1 K onder het doel reset de hold niet.

**Klep tijdens de run.**

#### `DHW legionella coil circulation`

- Standaard **aan**.

De klep blijft de hele run op DHW, zodat de pomp tankwater door de spiraal circuleert en de tank mengt. Zet dit uit zodra er een eigen circulatiepomp tussen top en bodem zit.

**Maximale duur.** Duurt de run langer dan **150 min**, dan volgt de fout `TIMEOUT`.

**Uitlezen:** `DHW legionella ETA` en `DHW legionella elapsed`.

**Warmtepompen.** Tijdens legionella draait de lead volgens [single-HP mode](#duo-of-single); de tweede-HP assist is uit.

### Handmatig forceren

Met `DHW source legionella force`, of de HA-proxy `binary_sensor.openquatt_ext_dhw_legionella_force`. Een handmatige force slaat alle uitstel over. Loopt er al een DHW-cyclus, dan start hij daarna. `DHW legionella deferral status` meldt wat er gebeurt.

### Naar een goedkoop uur trekken

#### `DHW legionella tariff aware`

- Standaard **uit**.

Staat de geplande run binnen `DHW legionella advance window` (**24 u**), en is het nu een goedkoop uur, dan start de run nu. Het goedkope uur komt uit, in volgorde van voorkeur:
1. HA `input_datetime` start/eind van het goedkoopste venster;
2. de HA-proxy `binary_sensor.openquatt_ext_dhw_legionella_cheap_window`;
3. het huidige tarief ten opzichte van het 24-uursgemiddelde, onder `DHW legionella cheap tariff ratio` (**0,7**).

> **Wat dit in de huidige firmware wel en niet doet.** De code bevat ook "wachten op een goedkoop uur" (`DHW legionella max wait for cheap`, 24 u) en "uitstellen na natuurlijke pasteurisatie" (`DHW smart legionella deferral`, `DHW legionella max defer`). Die controles werken alleen op een automatisch **vervroegde** run. De gewone wekelijkse run plant de toestandsmachine zelf, en die kijkt er niet naar.
>
> In de praktijk:
> - de wekelijkse run start op tijd, ook in een duur uur;
> - na een natuurlijke pasteurisatie start hij evengoed;
> - smart deferral voorkomt alleen dat een run naar voren wordt getrokken.
>
> "Natuurlijke pasteurisatie" betekent dat de **tankbodem** het legionelladoel heeft gehaald.

---

## Starts beperken

### Tijdvenster

#### `DHW window enable`

- Standaard **uit**.

Gewone cycli en solar boosts mogen alleen starten binnen het venster. Een lopende cyclus wordt niet onderbroken. Legionella en snelboost gelden niet.

> Zolang de klok ongeldig is (geen NTP), blokkeert het ingeschakelde venster alle starts.

#### `DHW window start hour` en `DHW window end hour`

- Standaard **0** en **7**.
- Het eindeuur hoort er niet meer bij.
- Een venster over middernacht, bijvoorbeeld 22 → 6, wordt herkend.
- Gelijke uren betekent: nooit starten.

| Start | Eind | Toegestaan |
|---|---|---|
| 0 | 7 | 00:00–07:00 |
| 22 | 6 | 22:00–06:00 |
| 0 | 0 | nooit |

### Dure uren vermijden

#### `DHW avoid expensive tariff`

- Standaard **uit**.

Binnen het duurste tariefvenster van de dag start geen gewone cyclus en geen solar boost. Het venster komt uit HA (`input_datetime` duurste start/eind, zie [HA-koppelingen](#ha-koppelingen)). Een lopende cyclus loopt door.

**Uitzondering:** zakt de tanktop onder `DHW emergency top temperature` (**35 °C**), dan wordt toch verwarmd.

**Uitlezen:** `DHW avoid expensive status`.

---

## Flowbewaking

#### `DHW flow min` en `DHW flow max`

| | Standaard | Bereik |
|---|---|---|
| `DHW flow min` | **750 l/h** | 300–1300 l/h |
| `DHW flow max` | **1800 l/h** | 600–1800 l/h |

Zolang een warmtepomp voor DHW draait (HP-fase, legionella-HP-fase, boost met warmtepomp), moet de flow tussen deze grenzen liggen. Zit hij er **30 s** aaneengesloten buiten, dan volgt `FLOW_OUT_OF_RANGE`. Staan min en max verkeerd om, dan worden ze omgedraaid.

Daarnaast geldt altijd de systeem-lowflowbewaking (250 L/h, 60 s).

---

## Fouten

Alle fouten zijn **gelatcht**: de regeling blijft in `FAULT` tot je op `DHW clear fault` drukt. In `FAULT` staat de klep op CV, is het element uit, en wordt er geen warmtepomp gevraagd.

| Fout | Oorzaak | Wanneer |
|---|---|---|
| `SENSOR_IMPLAUSIBLE` | zie [Sensoren en bronnen](#sensoren-en-bronnen) | altijd, ook in rust |
| `HP_FAULT` | HA-proxy `binary_sensor.openquatt_ext_dhw_hp_fault` (of `DHW source HP fault`), **of de watertemperatuur-trip** van de installatie | altijd, ook in rust |
| `VALVE_STUCK_CV` | klep niet in DHW-stand na 20 s en één herhaling | bij de start van een cyclus |
| `VALVE_MISMATCH` | klep meldt 10 s CV terwijl hij op DHW hoort te staan | tijdens een cyclus |
| `FLOW_OUT_OF_RANGE` | flow 30 s buiten `DHW flow min`/`max` | zolang een warmtepomp voor DHW draait |
| `TIMEOUT` | legionella-run langer dan 150 min | alleen legionella |
| `LOCKOUT` | `DHW lockout` gaat aan tijdens een cyclus | tijdens een cyclus; in rust blokkeert lockout alleen nieuwe starts |

> **Geen fout:**
> - een HP-fase die 180 min haalt: de regeling gaat door naar het natraject of rust;
> - een boost die 90 min haalt: telt op in `DHW boost timeouts`.

> Een watertemperatuur-trip leidt tot `HP_FAULT`, en die moet je daarna met de hand wissen.

---

## Handmatig en inbedrijfstelling

- **`DHW lockout`:** blokkeert nieuwe starts. Gaat hij aan tijdens een cyclus, dan volgt `LOCKOUT`. Ook via de HA-proxy `binary_sensor.openquatt_ext_dhw_lockout`.
- **`DHW manual test mode`:**
  - Met `DHW manual valve relay` en `DHW manual element relay` schakel je klep en element direct.
  - Er wordt geen warmtepomp gevraagd.
  - Alleen voor bekabelingscontrole; zet hem daarna weer uit.
- **`DHW source …`:** handmatige bronnen die gelden als er geen HA-proxy is. Het gaat om `hp fault`, `lockout`, `solar boost`, `legionella force` en de vier temperaturen.
- **CM6 (element only) en CM97 (ontluchten):** nemen klep en element over, en de DHW-toestandsmachine pauzeert. Zie [CM6](element-only-heating-cm6-v0.40.md) en [CM97](ontluchtingsprotocol-cm97-v0.40.md).

---

## Diagnostiek

| Entiteit | Inhoud |
|---|---|
| `DHW state` / `DHW state code` | toestand als tekst / code 0–5 |
| `DHW fault` / `DHW fault code` | fout als tekst / code 0–7 |
| `DHW target flow temp` | aanvoerdoel dat naar de warmtepomp gaat |
| `DHW HP request active` | warmtepomp draait nu voor DHW |
| `DHW block CV priority` | CV-vraag wordt onderdrukt |
| `DHW Element Active` | element staat aan |
| `DHW boost reden` | waarom de lopende boost draait |
| `DHW boost timeouts` | aantal boosts dat 90 min haalde |
| `DHW single HP lead` | vastgezette lead in single-HP mode |
| `DHW second HP assist status` | toestand van de tweede-HP assist |
| `DHW coil mapped level` | niveau na coil-mapping |
| `DHW HP thermisch vermogen` | geschat thermisch vermogen van de draaiende units |
| `DHW estimated time to ready` | minuten tot `DHW HP stop top`* |
| `DHW legionella laatste run` / `volgende run` | planning |
| `DHW legionella ETA` / `elapsed` | voortgang van een lopende run |
| `DHW legionella deferral status` | wat er met de legionella-planning gebeurt |
| `DHW avoid expensive status` | blokkade door het dure tariefvenster |
| `DHW cyclus COP`, `energie in`, `energie uit` | zie [rendement en tapdetectie](dhw-rendement-en-tapdetectie-v0.54.md) |

\* **Beperking van `DHW estimated time to ready`.** Hij rekent tot `DHW HP stop top` op de tanktop, terwijl de HP-fase stopt op een tankbodem van 52 °C. Tijdens de HP-fase valt hij daardoor meestal te kort uit.

---

## HA-koppelingen

In `openquatt/oq_substitutions_common.yaml`:

| Substitutie | Standaard entiteit | Gebruik |
|---|---|---|
| `ha_dhw_tank_top_entity_id` e.a. | `sensor.openquatt_ext_dhw_tank_top` … | terugval voor de vier temperaturen |
| `ha_dhw_valve_aux_entity_id` | `binary_sensor.openquatt_ext_dhw_valve_aux_cv` | klepstand (ruw) |
| `ha_dhw_hp_fault_entity_id` | `binary_sensor.openquatt_ext_dhw_hp_fault` | `HP_FAULT` |
| `ha_dhw_lockout_entity_id` | `binary_sensor.openquatt_ext_dhw_lockout` | lockout |
| `ha_dhw_solar_boost_entity_id` | `binary_sensor.openquatt_ext_dhw_solar_boost` | solar boost |
| `ha_dhw_legionella_force_entity_id` | `binary_sensor.openquatt_ext_dhw_legionella_force` | legionella forceren |
| `ha_dhw_legionella_cheap_window_entity_id` | `binary_sensor.openquatt_ext_dhw_legionella_cheap_window` | goedkoop uur (tweede bron) |
| `ha_dhw_legionella_cheap_start/end_entity_id` | `input_datetime.house_battery_strategy_dynamic_cheapest_start/end` | goedkoopste venster |
| `ha_dhw_expensive_start/end_entity_id` | `input_datetime.house_battery_strategy_dynamic_expensive_start/end` | duurste venster |
| `ha_electricity_tariff_entity_id` | `sensor.zonneplan_current_electricity_tariff` | solar boost op tarief, legionella-tarief |
