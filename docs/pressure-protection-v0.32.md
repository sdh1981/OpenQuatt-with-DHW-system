# Pressure Protection (R32) — v0.32

OpenQuatt v0.32 introduceert per-HP slimme hogedrukbeveiliging die complementair werkt aan de hardware HPS (High Pressure Switch) van de warmtepomp zelf. Doel: de gebruiker krijgt nooit een HPS-fout te zien bij normaal bedrijf, omdat OpenQuatt al ingrijpt vóór de hardware-grens.

## R32 referentie

| Druk-tier | Bar | Wat gebeurt fysisch |
|---|---|---|
| Normaal continu | < 38 | OK, geen actie |
| Soft cap (v0.32) | 38-40 | OpenQuatt verlaagt level |
| Hard cap (v0.32) | 40-41 | OpenQuatt forceert level 0 |
| **HPS-mirror (v0.32)** | **41-42** | OpenQuatt mirror-trip, log event |
| HP firmware HPS | 41-43 | Hardware-trip, error code op HP |
| Component design pressure | 43-45 | Componenten-rating |
| PRV (Pressure Relief Valve) | 45-48 | Mechanische afblaas |
| **Kritisch fysisch** | **57,8** | R32 buiten fasediagram |

OpenQuatt's drempels (38/40/41) zijn conservatief en zitten ruim binnen het veilige bedrijfsbereik. Bij correcte werking trip je nooit boven de 40 bar, ook niet bij DHW op level 5+ in koud weer.

## Hoe het werkt

### Per-HP gedrag

Voor elke HP (HP1 en in Duo ook HP2) leest OpenQuatt elke 5 seconden de condensor-druk uit (`hp1_condenser_pressure`, `hp2_condenser_pressure`). Op basis van de gemeten druk wordt een **tier** bepaald en een **level cap** toegepast:

| Tier | Drukbereik | Level cap | Wat gebeurt |
|---|---|---|---|
| 0 (OK) | < soft_bar | 10 (= geen cap) | Normaal operatie |
| 1 (soft) | soft..hard | ladder, zie onder | Level wordt stapsgewijs verlaagd, druk zakt |
| 2 (hard) | hard..hps | 0 (= stop) | Compressor stopt |
| 3 (HPS-mirror) | >= hps | 0 (= stop) | OpenQuatt mirror van hardware HPS |

De `level cap` komt terecht in het `level_cap` veld van `apply_level` in de thermal actuator. Daarmee wordt het pressure-cap gecombineerd met silent-cap en day-cap (de strengste wint).

### De zachte trap (gewijzigd in v0.52)

Tot v0.51 was de zachte cap `10 - reduce`, dus **9** met de default. Die bond nooit — levels boven 6 komen in de praktijk niet voor. De zachte trap deed daarmee niets en de eerste echte ingreep was de harde trap: een volledige stop.

Sinds v0.52 is het een ladder die terugregelt vanaf het niveau dat de unit werkelijk draait:

1. **Bij binnenkomst** eenmalig vastzetten op `laatst toegepast niveau − reduce`, ondergrens 1. Eenmalig, want zou hij elke ronde opnieuw vanaf "huidig" rekenen, dan loopt hij binnen een halve minuut naar 1.
2. **Daarna** per `Pressure soft cap step time` (default 120 s) nog een level eraf, maar alleen zolang de druk nog stijgt.
3. **Bij een harde trip of HPS-mirror** blijft de ladder gelatcht — dat niveau was aantoonbaar te hoog.
4. **Loslaten** alleen bij terugval naar tier 0.

### Hysterese (nieuw in v0.52)

Deze module had, anders dan de aanvoerbeveiliging, helemaal geen hysterese. Daardoor flappert de tier op de drempel en zou de ladder hierboven telkens opnieuw beginnen. `Pressure release hysteresis` (default 1,5 bar) sluit dat gat: eenmaal gecapt moet de druk zover onder `soft_bar` zakken voor de cap loslaat.

### Edge-getriggerde event-telling

Bij elke **stijging** in tier wordt een event geteld:
- `Pressure soft events 24h` — aantal keer dat een HP boven 38 bar kwam
- `Pressure hard events 24h` — aantal keer dat een HP boven 40 bar kwam
- `Pressure HPS events 24h` — aantal keer dat een HP boven 41 bar kwam

Daarnaast zijn er lifetime-counters die niet automatisch resetten.

24h-counters resetten automatisch elke 24 uur.

### Peak EMA per HP

Een trage EMA (τ ≈ 1h, met snelle-rise-langzame-decay) houdt de "typische piekdruk" per HP bij. Stijgt deze waarde langzaam over weken/maanden, dan kan dat duiden op:
- Buitenunit vervuiling (verminderde warmte-afgifte van condensor)
- Refrigerant-overcharge (extra koelmiddel vult condensor te ver)
- Verstopte filter / lage primary-loop flow

## Default-instellingen

| Parameter | Default | Range | Effect |
|---|---|---|---|
| `Pressure protection enable` | **aan** | aan/uit | Master switch |
| `Pressure soft cap` | 38,0 bar | 30-42 | Drempel voor level-reductie |
| `Pressure hard cap` | 40,0 bar | 35-43 | Drempel voor stop |
| `Pressure HPS mirror` | 41,0 bar | 38-45 | OpenQuatt-mirror HPS trip |
| `Pressure soft cap reduce levels` | 1 | 1-4 | Eerste stap van de ladder, onder het draaiende niveau |
| `Pressure soft cap step time` | 120 s | 0-600 | Tijd per vervolgstap; 0 = alleen de eerste stap |
| `Pressure release hysteresis` | 1,5 bar | 0,5-5 | Drukdaling nodig voor cap-release |

## Diagnose-entiteiten

**Sensors:**
- `sensor.openquatt_pressure_hp1_peak_ema` — actuele peak EMA HP1 in bar
- `sensor.openquatt_pressure_hp2_peak_ema` — idem HP2 (Duo)
- `sensor.openquatt_pressure_hp1_level_cap` — actuele level cap (10 = geen, 0 = stop)
- `sensor.openquatt_pressure_hp2_level_cap` — idem HP2
- `sensor.openquatt_pressure_soft_events_24h` — soft cap events afgelopen 24h
- `sensor.openquatt_pressure_hard_events_24h` — hard cap events
- `sensor.openquatt_pressure_hps_events_24h` — HPS-mirror events

**Binary sensors (waarschuwingen):**
- `binary_sensor.openquatt_pressure_soft_warning_active` — actief als enige HP soft-cap heeft
- `binary_sensor.openquatt_pressure_hard_warning_active` — actief als enige HP op stop staat
- `binary_sensor.openquatt_pressure_fouling_warning` — actief als peak EMA > 36 bar OF ≥5 soft events in 24h

**Buttons:**
- `button.openquatt_pressure_reset_event_counters` — wist 24h + lifetime counters
- `button.openquatt_pressure_reset_peak_ema` — wist peak EMA per HP

## Wanneer events normaal zijn vs zorg

| Events 24h | Beeld |
|---|---|
| 0 soft / 0 hard / 0 HPS | Perfect, geen stress |
| 1-3 soft / 0 hard / 0 HPS | Mild, hoogte-pieken bij DHW of koud weer |
| 5+ soft / 0 hard / 0 HPS | Frequente soft-caps — check Tsup, flow, eventueel vervuiling |
| 1+ hard / 0 HPS | OpenQuatt grijpt regelmatig in — onderzoek nodig |
| 1+ HPS-mirror | Equivalent aan een hardware HPS trip — direct kijken naar oorzaak |

## Trend monitoring via peak EMA

Bij gezonde unit zou je peak EMA zien zoals:
- Mild weer (Tamb > 10°C, geen DHW): 22-28 bar
- Heating in kou (Tamb 0°C): 28-34 bar
- DHW level 5 (Tsup 55°C): 32-38 bar
- Combinatie kou + DHW: tot 38-40 bar incidenteel

Stijgt de EMA structureel boven 36 bar, dan triggert de `fouling_warning` binary sensor. Mogelijke oorzaken:
1. **Vervuilde buitenunit** — schoonmaken
2. **Refrigerant overcharge** — service door installateur
3. **Lage flow primary loop** — check pomp, autotune
4. **Filter verstopt** — vervangen
5. **Defrost-frequentie te laag** — koudere condities dan firmware verwacht

## Veiligheid

- Default `aan` — staat meteen aan na flashen, geen actie nodig om beschermd te zijn
- De drempels zitten ruim onder de hardware HPS — geen "false positive trips"
- Uitschakelen van de switch deactiveert ALLE drempels — dan ben je afhankelijk van alleen de hardware HPS
- Hard cap = level 0 voorkomt schade aan compressor seals en olie-degradatie
- HPS-mirror is alleen een log-event; in normaal bedrijf zou je deze tier nooit moeten zien voor je hardware HPS al heeft getript

## Verhouding tot OpenQuatt's andere caps

OpenQuatt's `apply_level` in de thermal_actuator kiest het **strengste** van:
- `oq_silent_max_level` (silent mode cap)
- `oq_day_max_level` (day-time max)
- `oq_pressure_max_level_hpX` (deze pressure cap)

Plus level-exclusies (`hp1_excluded_level_a/b`) die specifieke levels blokkeren.

De pressure cap werkt onafhankelijk en domineert wanneer hij strenger is. Bij `pressure_cap = 0` gaat de level naar 0 ongeacht andere instellingen.
