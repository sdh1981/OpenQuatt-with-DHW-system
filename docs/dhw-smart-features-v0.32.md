# DHW Smart Features v0.32

Naast de bestaande DHW-instellingen (zie [DHW: werking en instellingen](dhw-instellingen.md)) kwamen er in v0.32 zes slimme features bij, plus single-HP mode voor Duo. Hieronder staat hoe ze in de huidige firmware werken.

## Overzicht

| # | Feature | Type | Default | HA-input nodig |
|---|---|---|---|---|
| 1 | Tariff-aware DHW scheduling | actief | uit | ja, dynamisch tarief |
| 2 | PV-zelfconsumptie DHW | actief | uit | ja, grid net power |
| 3 | Adaptive usage pattern learning | leer + actief | uit | nee |
| 4 | Tank standby-loss tracker | observatie | aan | nee |
| 5 | Smart legionella deferral | beperkt, zie §5 | uit | nee |
| 6 | DHW time-to-ready sensor | observatie | aan | nee |
| 7 | DHW single-HP mode (Duo) | actief | uit | nee |

Features #1 tot en met #3 beïnvloeden de **effectieve `start_top_c`**: het temperatuur-niveau dat de DHW-toestandsmachine gebruikt om een nieuwe cyclus te starten. Door dit dynamisch op te schuiven, kan OpenQuatt:

- **eerder** starten als energie goedkoop is of er PV-overschot is (tank vullen voorraadachtig)
- **later** starten als energie duur is en de tank nog redelijk warm is
- **net op tijd** voorbereiden op een verwachte douche of bad

De totale shift wordt geclamped op `[30, hp_stop_top_c − 1 K]` zodat het systeem altijd binnen veilige grenzen blijft.

---

## 1. Tariff-aware DHW scheduling

**Wat:** zelfde principe als de Power House tariff-shift, maar dan op `start_top_c`. Bij dynamisch tarief (Zonneplan, Tibber, EPEX):

- **Goedkoop uur** (`tarief < cheap_ratio × 24h_gemiddelde`): verhoog `start_top` met max `preheat_max` °C — DHW kickt eerder, tank wordt vol geladen tijdens dalbodem
- **Duur uur** (`tarief > expensive_ratio × 24h_gemiddelde`): verlaag `start_top` met max `defer_max` °C — DHW wacht tot het tarief weer richting gemiddeld zakt

**Slewing:** 0,2 K/min. Een tarief-spike van 1 minuut beweegt de drempel dus maar 0,2 K — voorkomt geforceerde restarts.

**Tuning entiteiten:**
- `switch.openquatt_dhw_tariff_shift_enable` — aan/uit
- `number.openquatt_dhw_tariff_preheat_max` — default 3 °C
- `number.openquatt_dhw_tariff_defer_max` — default 2 °C
- `number.openquatt_dhw_tariff_cheap_ratio` — default 0,7
- `number.openquatt_dhw_tariff_expensive_ratio` — default 1,3

**HA setup:** zie het [Power House smart features pakket](power-house-smart-features-v0.32.md). Dezelfde tariff-sensoren worden hergebruikt voor DHW.

**Diagnose:** `sensor.openquatt_dhw_tariff_start_shift` — actueel toegepaste shift in °C.

---

## 2. PV-zelfconsumptie DHW

**Wat:** bij grid-export boven een drempel (default 1500 W) verhoogt OpenQuatt `start_top`, zodat een DHW-cyclus eerder start en de PV-export in warmte wordt omgezet. Daarnaast kan de export een solar boost met het element starten.

**Twee bewegingen:**
1. **`start_top`-shift:** lineair van 0 °C bij export = drempel naar `pv_max_shift` °C bij export = drempel + 3 kW. Werkt zodra deze feature aan staat.
2. **Element via solar boost:** bij export boven `DHW PV boost export threshold` (default **2700 W**). Dit werkt **alleen als ook `DHW auto boost enable` aan staat**, de hoofdschakelaar voor alle automatische boosts, standaard uit. Die staat bewust uit: PV-export, laag tarief en de HA-trigger pieken allemaal 's middags, en dan ging het element ongevraagd aan.

**Slewing:** 0,5 K/min op de start-shift, zodat een korte zonpiek niet meteen een laadcyclus start.

**Tuning entiteiten:**
- `switch.openquatt_dhw_pv_self_consumption_enable`
- `number.openquatt_dhw_pv_export_threshold` — default 1500 W (start-shift)
- `number.openquatt_dhw_pv_max_start_shift` — default 3 °C
- `number.openquatt_dhw_pv_boost_export_threshold` — default 2700 W, bereik 1000–6000 W (element)
- `switch.openquatt_dhw_auto_boost_enable` — hoofdschakelaar voor het element-deel

**HA setup:** zelfde grid net power proxy als bij Power House feature #2. Zie het smart-features pakket.

**Diagnose:** `sensor.openquatt_dhw_pv_start_shift` — actueel toegepaste shift in °C.

**Synergie:** als zowel #1 als #2 actief zijn op een zonnige goedkope dag, tellen beide shifts op. Beide samen gaan nooit boven `hp_stop_top_c − 1 K` zodat de cyclus altijd haalbaar blijft.

---

## 3. Adaptive usage pattern learning

**Wat:** OpenQuatt detecteert tap-events (water-aftap = bovenste tank-sensor zakt sneller dan 2 K binnen 5 min terwijl DHW idle is). Voor elk event wordt het uur-van-de-dag-bucket opgehoogd. Na een paar dagen ontstaat een 24-uurs patroon.

**Hoe wordt het gebruikt:** bij het inschatten van de start_top, kijkt OpenQuatt `lookahead_min` minuten vooruit (default 45 min). Als dat uur historisch hoge usage heeft, wordt `start_top` verhoogd met max `adaptive_max_shift` °C — zodat de tank op tijd warm staat voor je douche.

**Decay:** elk **uur** worden alle buckets met factor 0,985 vermenigvuldigd. Dat is een halfwaardetijd van ongeveer **46 uur**, niet 46 dagen zoals het code-commentaar zegt. Een tapping van een week geleden telt nog voor ongeveer 8 %. Het patroon volgt dus vooral de laatste paar dagen. Een bucket loopt op tot maximaal 10.

**Tap-event detectie (voor dit patroon):**
- Drop-drempel: 2 K binnen 5 min
- Debounce: één event per 10 min (een douche van 8 min telt als 1 event, niet als 8)
- Alleen tijdens IDLE_CV state, anders zou een DHW-cyclus zelf als event tellen
- Alleen actief als deze feature aan staat

Dit is een andere detectie dan de algemene [tapdetectie op daalsnelheid](dhw-rendement-en-tapdetectie-v0.54.md) (standaard aan). Die voedt de standby-loss-lerer; deze voedt het uurpatroon.

**Tuning entiteiten:**
- `switch.openquatt_dhw_adaptive_usage_learning`
- `number.openquatt_dhw_adaptive_lookahead` — default 45 min
- `number.openquatt_dhw_adaptive_max_preheat` — default 2 °C

**Reset:** `button.openquatt_dhw_reset_usage_learning` — wist alle buckets (gebruik na vakantie of grote routine-verandering).

**Diagnose:**
- `sensor.openquatt_dhw_adaptive_start_shift` — actueel toegepaste shift in °C
- `sensor.openquatt_dhw_upcoming_hour_usage` — events count voor het komende uur (disabled-by-default, enable in HA voor inzicht)

**Wanneer aanzetten:** na 1-2 weken normaal gebruik zijn de buckets vol genoeg. Tot die tijd doet de feature niets noemenswaardigs.

---

## 4. Tank standby-loss tracker

**Wat:** meet hoe snel de tank afkoelt tijdens stilstand (geen heating, geen tap-event). Berekent een UA_tank coëfficiënt (W/K) die je kunt vergelijken met de boiler-specificatie of als trend monitoren.

**Hoe:**
- Sample alleen tijdens `IDLE_CV` state én geen tap-event in laatste uur
- Meet temperature drop over rolling 30-min window
- `UA_sample = (ΔT × tank_thermal_capacity) / (Δt × ΔT_ambient)`
- Tank thermal capacity: `volume_L × 1,16 Wh/K` (water-eigenschap), kamer 20 °C
- EMA met τ ≈ 7 dagen voor stabiliteit
- Outlier rejection: alleen 0,2..15 W/K samples geaccepteerd

**Wat zegt het getal voor verschillende energie-labels (220L tank):**
- **A+ label** (~17 W gem.): UA ≈ 0,4 W/K
- **A label** (~25 W gem.): UA ≈ 0,6 W/K
- **B label** (~40 W gem.): UA ≈ 1,0 W/K
- **C label** (~62 W gem.): UA ≈ 1,5 W/K *(typisch Inventum Maxtank 220L)*
- **D label** (~85 W gem.): UA ≈ 2,1 W/K
- **>3 W/K**: slechte isolatie of een lekkage-issue

**Tuning:** stel `number.openquatt_dhw_tank_volume` correct in voor je tank (default 220L). Dat is de enige parameter die je hoeft aan te passen — de rest leert zichzelf.

**Reset:** `button.openquatt_dhw_reset_standby_loss_learning`. Gebruik na isolatie-update of tank-vervanging.

**Diagnose:**
- `sensor.openquatt_dhw_tank_standby_loss` — actuele UA_tank in W/K
- `sensor.openquatt_dhw_ua_samples` — aantal samples (disabled-by-default)

---

## 5. Smart legionella deferral

**Bedoeling:** heeft de hele tank recent al pasteurisatietemperatuur gehaald door normaal gebruik, dan is een legionella-run minder dringend en kan hij worden uitgesteld.

**Wat de firmware registreert:** elke keer dat de **tankbodem** het legionelladoel haalt (`DHW legionella target`, standaard 68 °C), wordt het tijdstip vastgelegd. Zonder bodemsensor wordt de top gebruikt. De bodem is de moeilijkste zone; haalt alleen de top 68 °C, dan telt dat niet als pasteurisatie. De interne naam `oq_dhw_natural_60c_last_seen_epoch_s` is een overblijfsel; de grens is het legionelladoel, niet 60 °C.

**Wat het in de huidige firmware wél en níét doet:**
- **Wel:** het voorkomt dat een run door de [tarief-bewuste legionella](dhw-instellingen.md#naar-een-goedkoop-uur-trekken) naar voren wordt getrokken.
- **Niet:** de gewone wekelijkse run uitstellen. Die plant de DHW-toestandsmachine zelf elke 7 dagen, en daar wordt deze registratie niet op toegepast. `DHW legionella deferral status` zal daarom in de praktijk niet "Uitgesteld - recent natuurlijke pasteurisatie" tonen.

Wie echt wil uitstellen, heeft dus nog een wijziging aan de planner nodig. Voor de veiligheid is de huidige situatie de conservatieve kant: de run gaat altijd door.

**Tuning:**
- `switch.openquatt_dhw_smart_legionella_deferral`
- `number.openquatt_dhw_legionella_max_defer` — venster in dagen (default 7, bereik 0–14)

---

## 6. DHW time-to-ready sensor

**Wat:** schat hoeveel minuten het duurt tot de tanktop `DHW HP stop top` bereikt. De schatting gebruikt:

- de huidige tanktop ten opzichte van `DHW HP stop top`;
- het thermische vermogen van de units die nu voor DHW draaien, op hun werkelijke niveau: `DHW HP thermisch vermogen`, uit de prestatiekaart, rekening houdend met single-HP mode, level bump, zachte aanloop en assist;
- plus 3000 W als het element aan staat;
- minus `UA_tank × (top − 20 °C)` als standby-verlies;
- de tankinhoud uit `number.openquatt_dhw_tank_volume` (default 220 L = 255 Wh/K).

**Sensor:** `sensor.openquatt_dhw_estimated_time_to_ready`, in minuten. NaN als er niet verwarmd wordt.

**Beperking:** het doel is `DHW HP stop top` op de tanktop (49 °C), terwijl de HP-fase stopt op een tankbodem van 52 °C. Tijdens de HP-fase valt de schatting daardoor meestal te kort uit. Bruikbaar als trend, niet als belofte.

**Use cases:**
- Dashboardtegel "Warm water over ~14 min"
- Vergelijking voor en na een isolatie-update of tankvervanging

---

## 7. DHW single-HP mode (Duo)

**Wat:** in een Duo-opstelling stuurt OpenQuatt de DHW-aanvraag standaard naar **beide** warmtepompen op hetzelfde niveau. Bij een laag DHW-niveau (1–3) levert dat de COP-arme toestand "Duo 1+1" op (COP ~2,1). Single-HP mode stuurt de hele aanvraag naar één unit, de **lead**. Een tweede unit springt alleen op meting bij.

**Hoe:**
1. `switch.openquatt_dhw_single_hp_mode` AAN
2. `number.openquatt_dhw_single_hp_level_bump`: hoeveel niveaus de lead extra krijgt (default **0** sinds v0.51.1, was 1; bereik 0–3)

> De bump telt bij het ingestelde `DHW HP level` op, niet bij een gemeten tekort. Met `DHW HP level` = 2 en bump 2 draait de lead op 4 zonder dat er iets om die capaciteit heeft gevraagd. De tweede-HP assist doet dat werk wél op meting: die komt pas als de tanktop aantoonbaar te traag stijgt. Wil je de lead structureel hoger, zet dan `DHW HP level` hoger; dan staat in één getal wat er echt gevraagd wordt.

**Welke unit lead is:** gekozen bij de start van elke cyclus en vastgezet tot het einde. Volgorde:
1. **Harde storing:** heeft één unit er een, dan wordt de andere lead.
2. **Frequentiebegrenzing:** knijpt één unit zichzelf af, dan wordt de andere lead.
3. **Draaiuren:** anders de unit met de minste draaiuren.

Alleen een harde storing op de lead zet de lead tijdens een cyclus over. Over een seizoen blijven de draaiuren zo ongeveer gelijk, zonder dat de vraag midden in een cyclus van unit wisselt.

**Tweede-HP assist:** loopt de tank te traag op terwijl de lead al op zijn volle niveau zit, dan springt de andere unit stapsgewijs bij, met eigen persgas- en water-uitbewaking. Volledige beschrijving: [DHW: werking en instellingen](dhw-instellingen.md#tweede-hp-assist).

**Voorbeeld scenario:**
- `oq_dhw_hp_level` = 3
- Duo mode: HP1=3 + HP2=3 → ~4760 W thermisch, COP ~2,5
- Single mode + bump 1: lead HP = level 4 alleen → ~2790 W thermisch, COP ~2,6
- Single mode + bump 0: lead HP = level 3 alleen → ~2380 W thermisch, COP ~2,5

**Wanneer aanzetten:**
- Een laag DHW-niveau (1–3), waar Duo 1+1 een slechte COP geeft
- Om slijtage te verminderen van de unit die in duo het warmste water ziet

**Wanneer niet aanzetten:**
- Een hoog DHW-niveau (5+), waar je vol vermogen wilt voor een korte laadtijd

**Wat er nog meer geldt:**
- De schakelaar geldt ook tijdens **legionella**: dan draait alleen de lead en is de assist uit.
- Tijdens een **snelboost** draaien altijd beide units.

**Reden-codes (in `Request Reason`):**
- `dhw_duo`: beide HPs (single mode uit)
- `dhw_single_hp1` / `dhw_single_hp2`: alleen de lead
- `dhw_single_hp1_assist` / `dhw_single_hp2_assist`: lead plus assist

---

## Aanbevolen volgorde van inschakelen

1. **Update naar v0.32** — features #4 en #6 staan aan, geen impact op gedrag
2. **Een week meedraaien** — observeer `tank_standby_loss` en `time_to_ready` om referentie te krijgen
3. **#3 Adaptive usage learning aan** — laat 1-2 weken leren voordat de shift effect heeft
4. **#1 Tariff-shift aan** — als je dynamisch tarief hebt en het smart-features HA-pakket hebt geconfigureerd
5. **#2 PV-zelfconsumptie aan** — als je PV hebt en de grid net power proxy werkt
6. **#5 Smart legionella deferral** heeft in de huidige firmware alleen effect in combinatie met de tarief-bewuste legionella (zie §5)

## Veiligheid

- Alle actieve features (#1, #2, #3, #7) staan default uit
- Effectieve `start_top` is altijd geclamped op `[30 °C, hp_stop_top_c − 1 K]`
- Tariff-shift en PV-shift worden geslewed: tariff-spikes of zonpieken kunnen geen instant start triggeren
- #4 en #6 zijn pure observatie — geen invloed op control output
- Minimum cycle rest (`oq_dhw_min_rest_s`) blijft van toepassing — features kunnen geen kort-cyclen veroorzaken
