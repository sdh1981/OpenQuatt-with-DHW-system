# Watertemperatuur beveiliging (per-HP, v0.32)

Per-HP soft/hard cap op compressor-level op basis van de **werkelijk gemeten** wateruittredetemperatuur van elke HP. Voorkomt dat één HP de aanvoer over de drempel duwt terwijl de andere nog koel is.

## Waarom een per-HP cap nodig is naast `max_water_temp_limit_c`

Het bestaande `max_water_temp_limit_c` (default 60°C) werkt **systeembreed via Power House P_target** — proportioneel terugmoduleren van de totale gevraagde warmte. Dat heeft drie beperkingen:

1. **Werkt alleen in Power House** — Curve-strategie krijgt geen direct effect
2. **System-niveau, niet per HP** — bij Duo serie zit HP2 (downstream) altijd warmer dan HP1. Bij 55°C HP2-uittree en 48°C HP1-uittree wil je alleen HP2 verlagen, niet beide
3. **Power-demand vermindering ≠ level reductie** — soms wil je niet de totale P verlagen maar specifiek het level cappen

Deze nieuwe module pakt al die punten aan op een per-HP basis.

## Tier-gedrag

Voor elke HP, gebaseerd op `hpX_water_out_temp`:

| Tier | Drempel | Level cap | Wat gebeurt |
|---|---|---|---|
| 0 (OK) | < soft_c | 10 (geen cap) | Normaal bedrijf |
| 1 (soft) | soft_c..hard_c | ladder, zie onder | Niveau wordt stapsgewijs verlaagd |
| 2 (hard) | >= hard_c | 0 (stop) | Compressor stopt direct |

Met defaults: soft_c = **55 °C**, hard_c = **58 °C**, reduce = **1 level**.

## De zachte trap (gewijzigd in v0.52)

Tot v0.51 werd de zachte cap berekend als `10 - reduce`, dus **9** met de default. Die cap bond nooit: levels boven 6 komen in de praktijk niet voor en DHW draait op 2 tot 5. De zachte trap deed daarmee niets, en de eerste echte ingreep was de harde trap — een volledige stop.

Sinds v0.52 is het een ladder die terugregelt vanaf het niveau dat de unit **werkelijk draait**:

1. **Bij binnenkomst** wordt de cap eenmalig vastgezet op `laatst toegepast niveau − reduce`, met 1 als ondergrens. Eenmalig, want zou hij elke ronde opnieuw vanaf "huidig" rekenen, dan loopt hij binnen een halve minuut naar 1 — de module draait elke 5 seconden.
2. **Daarna** gaat er per `soft cap step time` (default 120 s) nog een level af, maar **alleen zolang de temperatuur nog stijgt**. Werkt de eerste stap, dan blijft de ladder staan op het niveau waar het stabiliseert.
3. **Bij een harde trip** blijft de ladder gelatcht. Dat niveau was aantoonbaar te hoog, dus bij terugkeer naar de zachte trap begint hij niet opnieuw bovenaan.
4. **Loslaten** gebeurt alleen bij een terugval naar tier 0, en die wordt door de hysterese hieronder al tegengehouden.

Zet je `soft cap step time` op 0, dan blijft het bij de eerste, gelatchte stap.

## Hysterese

Eens een cap actief is, blijft die actief tot de uittree-temperatuur is gedaald tot `soft_c - release_c` (default release = 2 K, dus terug onder 53°C). Dit voorkomt level-flapping rond de drempel.

Voorbeeld:
- HP2 uittree stijgt naar 55,2°C → soft cap actief
- Daalt naar 54°C → cap blijft actief (boven 53°C release)
- Daalt naar 52,5°C → cap los, terug naar normaal

## Werking samen met andere caps

De thermal_actuator combineert alle caps via **min()**:

```
final_cap = min(silent_cap, day_cap, pressure_cap, supply_temp_cap)
```

Plus de level-exclusies. De strengste wint dus.

## Configuratie parameters

| Entiteit | Default | Range | Effect |
|---|---|---|---|
| `Supply temp protection enable` | **aan** | aan/uit | Master switch |
| `Supply temp soft cap` | 55,0 °C | 40-65 | Drempel voor level-reductie |
| `Supply temp hard cap` | 58,0 °C | 50-70 | Drempel voor stop |
| `Supply temp release hysteresis` | 2,0 K | 0,5-5 | Drop nodig voor cap-release |
| `Supply temp soft cap reduce levels` | 1 | 1-4 | Eerste stap van de ladder, onder het draaiende niveau |
| `Supply temp soft cap step time` | 120 s | 0-600 | Tijd per vervolgstap; 0 = alleen de eerste stap |

## Diagnose-entiteiten

**Sensoren:**
- `sensor.openquatt_supply_temp_hp1_outlet` — actuele uittree HP1 (°C)
- `sensor.openquatt_supply_temp_hp2_outlet` — idem HP2
- `sensor.openquatt_supply_temp_hp1_level_cap` — actuele level cap HP1 (10 = geen, 0 = stop)
- `sensor.openquatt_supply_temp_hp2_level_cap` — idem HP2
- `sensor.openquatt_supply_temp_soft_events_24h` — soft cap events 24h
- `sensor.openquatt_supply_temp_hard_events_24h` — hard cap events 24h

**Binary sensors:**
- `binary_sensor.openquatt_supply_temp_soft_cap_active` — actief bij elk HP in soft tier
- `binary_sensor.openquatt_supply_temp_hard_cap_active` — actief bij elk HP op level 0

**Knoppen:**
- `button.openquatt_supply_temp_reset_event_counters` — wist counters

## Typische gebruikssituaties

**Bij heating (CM2/CM3):**
- HP1 uittree gewoonlijk 32-42°C, HP2 36-46°C bij normale stooklijn
- Soft cap op 55°C: zelden bereikt, alleen bij hoge Tsup-targets (slechte isolatie + koud weer)
- Hard cap zal in heating mode quasi nooit triggeren

**Bij DHW (CM4):**
- HP1 uittree typisch 45-50°C (verwarmt water dat naar HP2 gaat)
- HP2 uittree typisch 52-58°C (eindelift naar tank)
- Soft cap op 55°C: HP2 kan in soft tier komen bij hogere DHW level (5-6)
- Dit is exact het scenario dat user beschrijft: HP2 throttle terug naar level 4, HP1 blijft volle gas → tank wordt nog steeds geladen maar zonder over-stress

**Voorbeeld scenario:**
```
T=0: DHW cyclus start, beide HPs op level 5
T=2 min: HP1 uittree 47°C, HP2 uittree 52°C — alles OK
T=8 min: HP1 uittree 50°C, HP2 uittree 55,5°C — HP2 soft cap actief
         → HP2 cap = 9 (uit max 10). Want level 5 staat al onder 9 → geen effect
         Maar als hp_req escaleert naar 6+: HP2 max bij 9 bevroren
T=10 min: HP1 uittree 51°C, HP2 uittree 57°C — soft cap actief, level 5 OK
T=12 min: HP1 uittree 52°C, HP2 uittree 58,5°C — HARD CAP HP2 → level 0
         HP1 blijft op 5, lade verder met halve capaciteit
T=14 min: HP2 uittree zakt naar 52°C (geen heating meer)
         <53°C release → HP2 cap los → HP2 kan weer aan
T=15 min: cycle stop, tank top op stop_top
```

In dit scenario:
- HP2 wordt beschermd tegen over-heating
- HP1 blijft werken (zijn uittree is lager)
- DHW-cyclus voltooit zonder hard system trip

## Veiligheid

- Default **aan** — werkt direct na flashen
- Werkt aanvullend op `max_water_temp_limit_c` (systeem-niveau)
- Werkt aanvullend op pressure protection (per-HP)
- Hard cap is **harder** dan `max_water_temp_limit_c` (60°C default) — kan op 58°C al stoppen, wat veiliger is voor componenten
- Hysterese voorkomt flapping
- Uitschakelen vervalt alleen deze laag — andere veiligheden blijven

## Verhouding tot bestaande water-temp logica

| Feature | Werkt op | Effect | Bereik |
|---|---|---|---|
| `max_water_temp_limit_c` | `oq_system_supply_temp` | Proportionele factor (P_target × 0..1) | Power House only |
| **Supply temp protection (deze)** | Per-HP `water_out_temp` | Per-HP level cap | Alle strategieën |
| Pressure protection | Per-HP condenser pressure | Per-HP level cap | Alle strategieën |

De drie werken samen en zijn complementair:
- `max_water_temp` reduceert demand vóórdat het ergens problematisch wordt
- Supply temp cap throttles individuele HP die out-of-band gaat
- Pressure cap is de laatste lijn voor mechanische bescherming
