# DHW Smart Features v0.32

Naast de bestaande DHW-instellingen (zie [DHW-instellingen](dhw-instellingen.md)) introduceert v0.32 zes slimme features. Vier daarvan grijpen actief in op de DHW-regeling, twee zijn observatie-features.

## Overzicht

| # | Feature | Type | Default | HA-input nodig |
|---|---|---|---|---|
| 1 | Tariff-aware DHW scheduling | actief | uit | ja, dynamisch tarief |
| 2 | PV-zelfconsumptie DHW | actief | uit | ja, grid net power |
| 3 | Adaptive usage pattern learning | leer + actief | uit | nee |
| 4 | Tank standby-loss tracker | observatie | aan | nee |
| 5 | Smart legionella deferral | actief | uit | nee |
| 6 | DHW time-to-ready sensor | observatie | aan | nee |

Alle vier de actieve features beïnvloeden de **effectieve `start_top_c`**: het temperatuur-niveau dat de DHW-toestandsmachine gebruikt om een nieuwe cyclus te starten. Door dit dynamisch op te schuiven, kan OpenQuatt:

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

**Wat:** bij grid-export boven een drempel (default 1500 W) verhoogt OpenQuatt `start_top` zodat een DHW-cyclus eerder kickt en de PV-export wordt omgezet in warmte. Daarnaast wordt het boost-element automatisch aan gezet wanneer de export ≥2,7 kW bedraagt (kan een 3 kW element praktisch volledig dekken).

**Twee bewegingen:**
1. `start_top` shift: linear van 0 °C bij export = drempel naar `pv_max_shift` °C bij export = drempel + 3 kW
2. Boost element forced ON: bij export > 2,7 kW (gewoonlijk dikke zon, vaak meerdere uren per dag)

**Slewing:** 0,5 K/min op de start-shift, zodat een korte zonpiek niet meteen een lade-cyclus triggert.

**Tuning entiteiten:**
- `switch.openquatt_dhw_pv_self_consumption_enable`
- `number.openquatt_dhw_pv_export_threshold` — default 1500 W
- `number.openquatt_dhw_pv_max_start_shift` — default 3 °C

**HA setup:** zelfde grid net power proxy als bij Power House feature #2. Zie het smart-features pakket.

**Diagnose:** `sensor.openquatt_dhw_pv_start_shift` — actueel toegepaste shift in °C.

**Synergie:** als zowel #1 als #2 actief zijn op een zonnige goedkope dag, tellen beide shifts op. Beide samen gaan nooit boven `hp_stop_top_c − 1 K` zodat de cyclus altijd haalbaar blijft.

---

## 3. Adaptive usage pattern learning

**Wat:** OpenQuatt detecteert tap-events (water-aftap = bovenste tank-sensor zakt sneller dan 2 K binnen 5 min terwijl DHW idle is). Voor elk event wordt het uur-van-de-dag-bucket opgehoogd. Na een paar dagen ontstaat een 24-uurs patroon.

**Hoe wordt het gebruikt:** bij het inschatten van de start_top, kijkt OpenQuatt `lookahead_min` minuten vooruit (default 45 min). Als dat uur historisch hoge usage heeft, wordt `start_top` verhoogd met max `adaptive_max_shift` °C — zodat de tank op tijd warm staat voor je douche.

**Decay:** elke ~hour worden alle buckets met factor 0,985 vermenigvuldigd. Dat geeft een halfwaardetijd van ~46 dagen. Na 1 maand stabiel patroon zijn nieuwe events nog ongeveer 70% even zwaar als oude — leerroutine blijft trapsgewijs adapteren als gewoontes wijzigen.

**Tap-event detectie:**
- Drop-drempel: 2 K binnen 5 min
- Debounce: één event per 10 min (douche van 8 min telt als 1 event, niet als 8)
- Alleen tijdens IDLE_CV state — anders zou een DHW-cyclus zelf als event tellen

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

**Wat:** als de tank top in de laatste 7 dagen minstens 60 °C heeft bereikt door normaal gebruik (wat regelmatig gebeurt op zonnige dagen met PV-boost of tijdens een hoge HP_stop), heeft OpenQuatt feitelijk al "legionella-vriendelijke" temperaturen gehad. De geplande wekelijkse legionella-run kan dan worden uitgesteld.

**Hoe wordt het toegepast:** in deze versie registreert OpenQuatt iedere keer dat de tank natuurlijk 60 °C bereikt (`oq_dhw_natural_60c_last_seen_epoch_s`). Het uitstellen zelf gebeurt via `oq_dhw_legionella_defer_days`. Wijzigingen aan de scheduler komen in een latere versie; voor nu is dit een markering die later gebruikt kan worden.

**Veiligheid:** de wettelijke minimum is 60 °C voor minstens 15 minuten. Deze feature **vervangt geen legionella-cyclus** — hij stelt hooguit uit. Dat is wettelijk nog steeds in orde zolang minstens 1× per 14 dagen 60+ wordt bereikt.

**Tuning:**
- `switch.openquatt_dhw_smart_legionella_deferral`
- `number.openquatt_dhw_legionella_max_defer` — max uitsteldagen (default 7)

---

## 7. DHW single-HP mode (Duo)

**Wat:** in Duo-opstelling routeert OpenQuatt standaard de DHW-aanvraag naar **beide** HPs op hetzelfde level. Bij lage DHW-level (1-3) levert dat de COP-arme "Duo 1+1"-toestand (COP ~2,1). Single-HP mode routeert de hele aanvraag naar één HP (de runtime-lead), met een optionele level-bump om de gemiste capaciteit te compenseren.

**Hoe:**
1. `switch.openquatt_dhw_single_hp_mode` AAN
2. `number.openquatt_dhw_single_hp_level_bump` zet hoeveel levels de lead HP omhoog mag (default 1, range 0-3)

**Voorbeeld scenario:**
- `oq_dhw_hp_level` = 3
- Duo mode: HP1=3 + HP2=3 → ~4760 W thermisch, COP ~2,5
- Single mode + bump 1: lead HP = level 4 alleen → ~2790 W thermisch, COP ~2,6
- Single mode + bump 0: lead HP = level 3 alleen → ~2380 W thermisch, COP ~2,5

**Wanneer aanzetten:**
- Lage DHW level (1-3) waar Duo 1+1 verdacht slechte COP geeft
- Korte DHW cycli waar tank niet ver leeg gaat — single is sneller bij level + bump
- Reduceer slijtage van de "trailing" HP (die warmer water ziet)

**Wanneer niet aanzetten:**
- Hoge DHW level (5+) waar je vol vermogen wilt voor snelle laadtijd
- Bij legionella-runs — daar wil je doorgaans beide HPs voor snelheid

**Reden-codes (in `Request reason`):**
- `dhw_duo` — beide HPs (single mode uit)
- `dhw_single_hp1` — alleen HP1 (lead), single mode aan
- `dhw_single_hp2` — alleen HP2 (lead), single mode aan

De runtime-lead wisselt automatisch op basis van draaiuren (`oq_runtime_lead_hp` sensor). Dat betekent dat HP1 en HP2 over een seizoen redelijk gelijke draaiuren behouden ondanks single-mode.

## 6. DHW time-to-ready sensor

**Wat:** schat hoeveel minuten tot tank top de stop-temperatuur bereikt, gebaseerd op:

- Huidige `tank_top` versus `hp_stop_top_c`
- Geschatte heating power: ~4500 W bij actieve HP-fase, +3000 W bij actief boost-element
- Aftrek van `UA_tank × (top − Tamb_room)` als standby-loss schatting
- Tank thermal capacity uit `number.openquatt_dhw_tank_volume` (default 220 L = 255 Wh/K)

**Sensor:** `sensor.openquatt_dhw_estimated_time_to_ready` — minuten tot ready, NaN als DHW niet actief is.

**Use cases:**
- Dashboard tegel: "Warm water over ~14 min"
- HA automation: pre-bericht voordat iemand zou willen douchen
- Vergelijking voor/na isolatie-update of tank-vervanging

---

## Aanbevolen volgorde van inschakelen

1. **Update naar v0.32** — features #4 en #6 staan aan, geen impact op gedrag
2. **Een week meedraaien** — observeer `tank_standby_loss` en `time_to_ready` om referentie te krijgen
3. **#3 Adaptive usage learning aan** — laat 1-2 weken leren voordat de shift effect heeft
4. **#1 Tariff-shift aan** — als je dynamisch tarief hebt en het smart-features HA-pakket hebt geconfigureerd
5. **#2 PV-zelfconsumptie aan** — als je PV hebt en de grid net power proxy werkt
6. **Optioneel #5 Smart legionella deferral** — alleen als je echt zeker weet dat je tank vaak 60+ haalt natuurlijk

## Veiligheid

- Alle actieve features (#1, #2, #3) staan default uit
- Effectieve `start_top` is altijd geclamped op `[30 °C, hp_stop_top_c − 1 K]`
- Tariff-shift en PV-shift worden geslewed: tariff-spikes of zonpieken kunnen geen instant start triggeren
- #4 en #6 zijn pure observatie — geen invloed op control output
- Minimum cycle rest (`oq_dhw_min_rest_s`) blijft van toepassing — features kunnen geen kort-cyclen veroorzaken
