# Element Only Heating — CM6 (v0.40)

## Inleiding

CM6 is een **exclusieve mode** waarin het 3 kW elektrische verwarmingselement
in de DHW-tank het tapwater opwarmt. **Geen warmtepomp, geen verwarming,
geen koeling** — alleen het element draait, thermostatisch gestuurd op een
configureerbaar temperatuurdoel met hysterese.

CM6 wordt geactiveerd via een **schakelaar** in Home Assistant — geen
override-select nodig.

---

## Wanneer gebruik je CM6?

| Scenario | Voorbeeld |
|----------|-----------|
| **HP-storing** | Compressor defect → element houdt tapwater warm |
| **Onderhoud aan HP** | Eenheid uit → tapwater blijft toch warm |
| **Goedkope nachtstroom** | Schakelaar via HA-schema 's nachts aan, overdag uit |
| **PV-overschot direct** | Schakelaar via HA template als PV-export > X kW |
| **Stille modus** | Compressor heeft te veel geluid 's avonds → element only |
| **Vakantie zuinig** | Lager doel (40°C) zonder HP-cycli |

CM6 is **niet** geschikt voor:
- Normale dagelijkse DHW-productie (gebruik CM4 = HP DHW, veel zuiniger)
- Snel opwarmen na vakantie (3 kW = traag, gebruik DHW boost ipv)
- Legionella-run (gebruik specifieke legionella mode, target ≥ 60°C)

---

## Werking

```
Schakelaar AAN
  ├─ Tank bottom < (target - δ)  → ELEMENT AAN, CM6 "Heating"
  ├─ Tank bottom ≥ target        → ELEMENT UIT, CM6 "Idle"
  └─ Tussen target-δ en target   → houdt huidige stand (hysterese)
  
Schakelaar UIT
  └─ Direct → ELEMENT UIT, mode terug naar AUTO
```

**Tijdens CM6**:
- ❌ Beide HP-compressors blijven in Standby (via `cm_allows_hp` gating)
- ❌ Geen CM2/CM3 (verwarmen via HP)
- ❌ Geen CM4 (DHW via HP)
- ❌ Geen CM5 (koeling)
- ✅ Element thermostatisch op `target_c` ± `off_delta_c`
- ⚪ 3-wegklep **wordt niet aangestuurd** (blijft in laatste stand — element
  zit in de tank en heeft geen flow nodig)
- ⚪ Pomp draait alleen op postflow/sticky logica (geen actieve circulatie)
- ⚪ DHW controller wordt **niet getickt** — state machine pauzeert en hervat
  bij CM6-exit vanaf zijn laatste positie
- ✅ Legionella-run (force) heeft nog steeds prioriteit boven CM6

---

## Prioriteit-volgorde

CM6 wint van alles behalve veiligheid en handmatige override:

```
1. CM98 (frost protection)                — vorstveiligheid wint altijd
2. CM97 (air purge override)              — handmatige diagnostiek
3. Legionella force (DHW state == 4)      — hygiëne-kritiek
4. CM6 (element only, switch on)          — gebruiker keuze ✦
5. CM4 (DHW HP)                           — normaal DHW
6. CM5 (cooling)                          — koel-vraag
7. CM2/CM3 (heating)                      — warmte-vraag
8. CM1 (pre/postflow)                     — overgangen ← wordt overgeslagen
9. CM0 (standby)                          — niets te doen
```

> CM6 neemt direct over zonder CM1 (preflow/postflow). De HPs gaan naar
> Standby waar ze hun eigen interne afsluitprocedure regelen — externe
> postflow is niet nodig omdat het element geen flow vereist. Pending
> CM1-timers worden bij CM6-entry gewist zodat er ook geen achtergebleven
> overgangsstap volgt.

---

## Entiteiten in Home Assistant

### Bediening
| Entity | Type | Default | Range | Doel |
|--------|------|---------|-------|------|
| `switch.openquatt_dhw_element_only_heating` | switch | **OFF** | aan/uit | Hoofdschakelaar |
| `number.openquatt_dhw_element_only_target` | number | **50°C** | 35–65°C | Streeftemperatuur tank bottom |
| `number.openquatt_dhw_element_only_off_delta` | number | **5°C** | 1–10°C | Hysterese (restart-marge) |

### Diagnose
| Entity | Type | Toont |
|--------|------|-------|
| `sensor.openquatt_dhw_element_only_state` | text | "Off" / "Waiting" / "Heating" / "Idle (target reached)" |
| `sensor.openquatt_dhw_element_only_elapsed` | number (min) | Hoe lang CM6 al loopt |
| `sensor.openquatt_control_mode_label` | text | "CM6 - DHW - Element Only" |

---

## Voorbeelden van gebruik

### Voorbeeld 1 — Handmatig aan/uit
1. HA → toggle `switch.openquatt_dhw_element_only_heating` AAN
2. Tank koud? → Element gaat direct aan
3. Tank op target? → Element uit, status "Idle"
4. Bij gebruik (douche) → tank koelt → onder hysterese → element aan
5. Toggle UIT → element direct uit, terug naar AUTO

### Voorbeeld 2 — Nacht-schedule via HA automation
```yaml
# HA configuration.yaml of automation
automation:
  - alias: "Element only 's nachts"
    trigger:
      - platform: time
        at: "02:00:00"
    action:
      - service: switch.turn_on
        target:
          entity_id: switch.openquatt_dhw_element_only_heating
      - service: number.set_value
        target:
          entity_id: number.openquatt_dhw_element_only_target
        data:
          value: 55  # iets hoger 's nachts

  - alias: "Element only 's ochtends uit"
    trigger:
      - platform: time
        at: "06:00:00"
    action:
      - service: switch.turn_off
        target:
          entity_id: switch.openquatt_dhw_element_only_heating
```

### Voorbeeld 3 — PV-overschot
```yaml
automation:
  - alias: "Element only bij PV overschot"
    trigger:
      - platform: numeric_state
        entity_id: sensor.solar_export_power_w
        above: 3000
        for: "00:05:00"  # 5 min stabiel boven 3kW
    action:
      - service: switch.turn_on
        target:
          entity_id: switch.openquatt_dhw_element_only_heating

  - alias: "Element only uit bij PV onder drempel"
    trigger:
      - platform: numeric_state
        entity_id: sensor.solar_export_power_w
        below: 1000
        for: "00:10:00"
    action:
      - service: switch.turn_off
        target:
          entity_id: switch.openquatt_dhw_element_only_heating
```

---

## Veiligheidsoverwegingen

| Veiligheid | Werking |
|-----------|---------|
| Tank sensor NAN | Element wordt direct uitgeschakeld (geen droogkoken) |
| Max target gelimiteerd op 65°C | Voorkomt onbedoeld te heet water (legionella-zone is 60°C+) |
| Switch uit = direct uit | Geen runtime-vergrendeling, instant stop |
| Legionella-run prioriteit | Hygiëne wint van CM6 |
| Lockout/HP fault prioriteit | Systeem-veiligheid wint |
| Power cap (16A) respect | Bij overschrijding wordt CM6 idle gemaakt |
| Hysterese | Voorkomt korte aan/uit cycli (default 5°C ondertussen-band) |

> **Belangrijk**: dit is **geen** vervanging voor de wekelijkse legionella-run.
> Voor hygiëne moet de tank periodiek > 60°C bereiken. CM6 met target 50°C
> houdt water comfortabel warm maar pasteuriseert niet.

---

## Verbruik en kosten-indicatie

| Tank van 220L, ΔT 10°C (40 → 50°C) | Met element |
|----------------------------------|-------------|
| Energie nodig | 2.55 kWh |
| Vermogen | 3 kW |
| Tijd | ~50 min |
| Kosten (0.25 EUR/kWh) | ~€0.64 |
| Equivalent met HP (COP 2.5) | ~1.0 kWh = ~€0.25 |

→ Element kost **~2.5× zoveel** als HP per cyclus. Gebruik CM6 dus **bewust**,
niet als dagelijkse standaard.

---

## Technische details

### Architectuur

```
oq_supervisory_controlmode.yaml
  ├─ switch: oq_dhw_element_only_enable
  ├─ number: oq_dhw_element_only_target_c, oq_dhw_element_only_off_delta_c
  ├─ globals: oq_cm6_element_on, oq_cm6_run_started_ms
  ├─ desired_cm logic:
  │   if (switch on AND not legionella AND not CM1) → desired = 6
  ├─ thermostatic loop (every supervisor tick ~5s):
  │   on entry: element_on = (bottom < target)
  │   currently_on: stop when bottom >= target
  │   currently_off: start when bottom < (target - off_delta)
  └─ sensors: state text, elapsed time

oq_boiler_control.yaml
  └─ if (cm_code == 6):       ← BEFORE controller.tick()
      ├─ boiler_relay → follows oq_cm6_element_on
      └─ return (skip DHW state machine + all other side-effects)
      
      • Valve / hp_request / block_cv_priority worden NIET aangeraakt
      • DHW state machine pauzeert volledig

oq_heat_control.yaml / oq_thermal_request_control.yaml
  └─ cm_allows_hp = (cm == 2 || 3 || 4 || 5)  ← CM6 valt buiten
      └─ HPs naar Standby + level 0 ✅
```

### Toestandsdiagram

```
                  ┌──────────────┐
                  │  Switch OFF  │  ← initial
                  └──────┬───────┘
                         │ switch ON
                         ▼
                  ┌──────────────┐
            ┌────▶│  Waiting     │  (CM6 not yet active, e.g. CM1 postflow)
            │     └──────┬───────┘
            │            │ CM6 active
            │            ▼
            │     ┌──────────────┐    bottom < (target-δ)    ┌──────────────┐
            │     │  Idle        │ ────────────────────────▶ │  Heating     │
            │     │ (element off)│                           │ (element on) │
            │     └──────┬───────┘ ◀───────────────────────  └──────┬───────┘
            │            │             bottom >= target              │
            │            │                                           │
            └────────────┴───────────────────────────────────────────┘
                  switch OFF (any state)
```

### Globals / entities mapping

| Naam | Type | Range | Functie |
|------|------|-------|---------|
| `oq_dhw_element_only_enable` | switch (bool) | aan/uit | Hoofdschakelaar |
| `oq_dhw_element_only_target_c` | number (float) | 35–65 | Doeltemperatuur |
| `oq_dhw_element_only_off_delta_c` | number (float) | 1–10 | Hysterese |
| `oq_cm6_element_on` | global bool | — | Element on/off vlag |
| `oq_cm6_run_started_ms` | global uint32_t | — | Start tijdstip in millis() |
| `oq_cm6_state_text` | text_sensor | — | UI status string |
| `oq_cm6_elapsed_min` | sensor (float) | — | Verstreken tijd in minuten |

### Pseudocode thermostatic loop

```cpp
if (cm_code == 6) {
  const float tb = tank_bottom_c;
  const float target = oq_dhw_element_only_target_c;
  const float delta = oq_dhw_element_only_off_delta_c;

  if (isnan(tb)) {
    oq_cm6_element_on = false;       // safety
  } else if (just_entered_cm6) {
    oq_cm6_element_on = (tb < target);  // bootstrap on entry
  } else if (tb >= target) {
    oq_cm6_element_on = false;       // target reached → off
  } else if (tb < (target - delta)) {
    oq_cm6_element_on = true;        // below hysteresis → on
  }
  // else: stay in current state (hysteresis band)
}
```

---

## Wijzigingsgeschiedenis

| Versie | Wijziging |
|--------|-----------|
| v0.40.0 | **Initial release** — CM6 Element Only Heating mode toegevoegd |
