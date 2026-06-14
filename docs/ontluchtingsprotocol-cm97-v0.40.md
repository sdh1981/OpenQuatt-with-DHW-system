# Ontluchtingsprotocol — CM97 (v0.40)

## Inleiding

Na het vullen of bijvullen van het hydraulische systeem (CV-verzamelaars,
vloerlussen, HP-circuit, DHW-spiraal) blijft er **lucht** achter op hoge
punten en in bochten. Lucht in het systeem leidt tot:

- Verminderde warmteoverdracht (luchtkussens in de vloerlussen)
- Ruis/sissen in leidingen
- Cavitatie in de pomp
- Foutmeldingen op de HP (lage flow ondanks normaal toerental)
- Onbetrouwbare flow-metingen

OpenQuatt v0.40 bevat een **geautomatiseerd ontluchtingsprotocol** dat via
de override **"Force CM97"** wordt gestart. Het protocol stuurt de pomp en
de 3-wegklep volgens een vast schema zodat lucht naar de automatische
ontluchters wordt verplaatst.

---

## Wat doet CM97?

CM97 is een **veiligheidsmode** waarbij de compressors **niet** draaien.
Alleen de pomp(en) en de 3-wegklep zijn actief. De DHW-state machine en
heating/cooling logic worden tijdens CM97 buiten werking gezet.

### iPWM-conventie (belangrijk!)

OpenQuatt stuurt de Quatt-pomp aan via **Profile C PWM**, wat een
**inverse** signalering is — hogere iPWM = lager toerental:

| iPWM | Pomp-toerental | Typische flow |
|------|----------------|---------------|
| 0    | **100% (max)** | ~max systeem flow |
| 50   | ~95% | ~bijna max |
| 1000 | **0% (stop)** | 0 L/h |

### Multi-cycle protocol — 3 cycli over default 12 minuten

Het protocol doet **3 volledige cycli** waarbij elke cyclus bestaat uit:

| Sub-fase | Duur (% van cyclus) | 3-wegklep | Pomp iPWM | Doel |
|----------|---------------------|-----------|-----------|------|
| **Settle** | 20% (~48s @ 12 min) | CV (wordt hier gezet) | **1000** = pomp uit | Lucht stijgt vrij op naar automatische ontluchter, klepwisseling tijdens pomp-uit (geen schok) |
| **CV pulse** | 40% (~96s @ 12 min) | CV | 20s @ **50** / 10s @ **1000** (pulse) | Drukpieken schudden lucht los uit CV-vloerlussen |
| **DHW pulse** | 40% (~96s @ 12 min) | DHW | 20s @ **50** / 10s @ **1000** (pulse) | Identiek patroon maar door DHW-spiraal |

Bij default 12 min totale duur duurt elke cyclus **~4 minuten** (48s settle + 96s CV pulse + 96s DHW pulse). Het hele protocol bestaat dus uit **3 × deze cyclus = 9 fases**:

| Fase | Naam |
|------|------|
| 1 | Cyclus 1 - Settle |
| 2 | Cyclus 1 - CV pulse |
| 3 | Cyclus 1 - DHW pulse |
| 4 | Cyclus 2 - Settle |
| 5 | Cyclus 2 - CV pulse |
| 6 | Cyclus 2 - DHW pulse |
| 7 | Cyclus 3 - Settle |
| 8 | Cyclus 3 - CV pulse |
| 9 | Cyclus 3 - DHW pulse |

### Waarom multi-cycle beter werkt

**Single-pass (oude protocol):**
- 1× CV → 1× DHW → klaar
- Lucht die tijdens DHW-fase opnieuw in oplossing gaat, blijft achter
- Geen pauze om lucht vrij te laten komen uit oplossing

**Multi-cycle (nieuwe protocol):**
- 3× herhaling met settle-pauzes ertussen
- **Henry's wet**: tijdens settle-pauzes komt opgelost gas vrij (drukval)
- Volgende pulse-fase vangt die nieuwe lucht op
- 3 passes = 3 generaties lucht opgeruimd
- Klepwisseling tijdens settle = pomp uit = geen mechanische schok

### Watersnelheid bij verschillende leidingdiameters

Tijdens de pulse-piek (iPWM 50 = max toerental) wordt lucht **meegesleept**
in de flow naar de automatische ontluchter. Daar gaat het systeem mee
om — hoge snelheid is gewenst:

| Flow totaal | 22 mm (ID 20 mm) | 28 mm (ID 26 mm) |
|------------:|:----------------:|:----------------:|
| 300 L/h | 0.27 m/s | 0.16 m/s |
| 500 L/h | 0.44 m/s | 0.26 m/s |
| **800 L/h+** | **0.71 m/s ✅ pulse** | 0.42 m/s ✅ pulse |
| 1000 L/h+ | 0.88 m/s ✅ pulse | 0.52 m/s ✅ pulse |

Tijdens **settle** (iPWM 1000 = pomp uit) is de flow nul, snelheid 0 m/s —
maximale ruimte voor lucht om vrij te stijgen.

De **totale duur is configureerbaar** (3–30 min). De faseverdeling
schaalt mee: fase 1 = 20%, fase 2 = 40%, fase 3 = 40% van de totale tijd.

### Wat gebeurt er níet tijdens CM97?

- ❌ Beide compressors blijven in **Standby** (HW-niveau)
- ❌ 3 kW DHW-element wordt **expliciet uitgezet**
- ❌ DHW-aanvraag (CM4) wordt **niet gestart**, ook bij koude tank
- ❌ Heating-aanvraag (CM2) wordt **niet gehonoreerd**
- ❌ Cooling (CM5) wordt **niet gestart**
- ❌ Legionella-run wordt **niet gestart**

### Auto-expire

Na het instelbare aantal minuten zet CM97 zichzelf **automatisch uit**
en valt het systeem terug op `Auto` (normaal CM0 als er geen vraag is).
De override hoeft niet handmatig teruggezet te worden.

---

## Wanneer gebruik je CM97?

| Situatie | CM97 gebruiken? |
|----------|-----------------|
| Eerste vulling na installatie | ✅ Ja |
| Bijvullen na drukdaling | ✅ Ja |
| Onderhoud waarbij water werd afgetapt | ✅ Ja |
| Vervangen van filter / pomp / componenten | ✅ Ja |
| Pomp maakt gorgelend geluid | ✅ Ja (luchtindicatie) |
| Vloer voelt op sommige plekken ineens koud aan | ✅ Mogelijk lucht in de lus |
| Routine onderhoud (jaarlijks) | ⚠️ Niet nodig, kan wel |
| Tijdens actieve verwarming | ❌ Nee, eerst stoppen |
| Bij vorstkans (CM98 actief) | ❌ Niet starten |

---

## Stap-voor-stap gebruik

### 1. Voorbereiding

- ✅ Druk in het systeem ≥ 1.0 bar (controleer manometer)
- ✅ Alle radiator/manifold-ontluchters gesloten (de **automatische** ontluchter doet het werk)
- ✅ HA-verbinding actief, OpenQuatt entiteiten zichtbaar
- ✅ Geen vorst voorspeld in komende uren
- ⚠️ Plaats handdoek/emmer onder eventuele handmatige ontluchters
- ⚠️ Verzeker dat er een vulslang in de buurt is voor het geval druk daalt

### 2. (Optioneel) duur aanpassen

In HA → **Air Purge Duration** → kies tussen 5 en 30 minuten. De 3 cycli
en sub-fases schalen proportioneel mee met de totale duur:

- **5 min** (minimum): snelle re-purge — 3× cyclus van ~1.7 min elk
- **12 min** (default): standaard na vulling — 3× cyclus van ~4 min elk
- **18 min**: na flink onderhoud, meerdere componenten vervangen
- **30 min**: zware lucht-problematiek, na complete drainage

### 3. Start het protocol

In HA → **CM Override** → kies **"Force CM97"**

Direct daarna gebeurt het volgende:
- Beide HPs worden naar **Standby** geforceerd (compressorlevel = 0)
- De 3-wegklep gaat naar **CV-stand** (fase 1 begint)
- Pomp(en) draaien op iPWM 250
- Op het dashboard verschijnt:
  - `ControlMode` → "CM97 - Air Purge Protocol"
  - `Air Purge Phase Text` → "Fase 1 - CV lage flow"
  - `Air Purge Remaining` → afteller in seconden

### 4. Monitor het verloop

| Sensor | Wat je ziet |
|--------|-------------|
| `Air Purge Phase Text` | Huidige fase (text) |
| `Air Purge Phase` | Fase nummer (1, 2 of 3) |
| `Air Purge Remaining` | Resterende seconden tot einde |
| `Flow average` | Werkelijke flow in L/h |
| `dhw_valve_aux_dhw_position` | Klepstand (uit=CV, aan=DHW) |

**Hoorbare verschijnselen tijdens het protocol:**
- Fase 1: rustige pomp
- Fase 2: **pulsende pomp** (elke 40 sec een puls) — *normaal*
- Fase 3: klepschakeling naar DHW (1× hoorbaar), opnieuw pulserende pomp
- Soms gorgelen of borrelen in leidingen: **dat is precies wat we willen** — lucht beweegt

### 5. Voltooiing

Na het instelbare aantal minuten:
- ControlMode springt terug naar **CM0 - Standby** (of CM1 als pump-postflow nog loopt)
- CM Override schakelt **automatisch** terug naar "Auto"
- Pomp stopt (of valt terug op normaal sticky-pump gedrag)

### 6. Naverificatie

Controleer na het protocol:
- ✅ Druk nog steeds ≥ 1.0 bar (lucht is verdwenen, geen waterverlies)
- ✅ Geen pomp-geluiden meer bij normale operatie
- ✅ Flow-sensor leest stabiele waarden
- ⚠️ Bij druk-daling: druk bijvullen tot 1.5 bar en CM97 nog een keer draaien

---

## Handmatig stoppen

Wil je het protocol vóór de geplande einde stoppen?

In HA → **CM Override** → kies **"Auto"**.

Het systeem keert direct terug naar normale werking. Geen koude start
nodig, geen fault states. Veilig.

---

## Veelvoorkomende issues

### Druk daalt tijdens of na CM97
- **Oorzaak**: lucht is vervangen door water dat ergens naartoe is gegaan (uit het systeem via automatische ontluchter)
- **Actie**: bijvullen tot 1.5 bar, CM97 herhalen
- **Normaal** bij eerste vulling van een groot systeem (verwacht 0.2–0.4 bar daling)

### Pomp toont "Out of range" of fault
- **Oorzaak**: nog steeds te veel lucht, flow zit onder min_flow
- **Actie**: stop CM97, vul druk bij, herhaal met langere duur (15 min)

### Geen klepwisseling waarneembaar in fase 3
- **Oorzaak**: klep-relay output werkt niet, of klep mechanisch vast
- **Actie**: test handmatig via `switch.openquatt_dhw_valve_relay`

### Compressor probeert toch te starten
- **Oorzaak**: bug — zou niet mogen gebeuren tijdens CM97
- **Actie**: log opslaan, stop CM97 onmiddellijk, rapporteer issue

---

## Technische details (voor ontwikkelaars)

### Architectuur

```
oq_supervisory_controlmode.yaml
  └─ select "CM Override" optie 4 → desired_cm = 97
  └─ globals: oq_cm97_active_since_ms, oq_purge_phase, oq_purge_target_ipwm, oq_purge_target_valve_dhw
  └─ interval 1s: purge state machine → schrijft target_ipwm + target_valve
  └─ auto-expire wanneer elapsed >= duration

oq_flow_control.yaml
  └─ early-return wanneer cm_code == 97
     └─ schrijft oq_purge_target_ipwm direct naar pomp(en) via write_pump_pwm_if_changed()

oq_boiler_control.yaml
  └─ early-return wanneer cm_code == 97
     └─ klep volgt oq_purge_target_valve_dhw
     └─ boiler_relay forced OFF, dhw_hp_request_active = false

oq_heat_control.yaml / oq_thermal_request_control.yaml
  └─ cm_allows_hp = (cm == 2 || 3 || 4 || 5) → 97 valt automatisch buiten
     └─ HPs naar Standby + compressor level 0 via reguliere "outside CM" logica
```

### Globals/entities overzicht

| Naam | Type | Range / Domein | Functie |
|------|------|----------------|---------|
| `oq_cm97_active_since_ms` | global uint32_t | — | Starttijd in millis() |
| `oq_purge_phase` | global int | 0/1/2/3 | Actieve fase |
| `oq_purge_target_ipwm` | global int | 0–1000 | Target iPWM voor de pomp |
| `oq_purge_target_valve_dhw` | global bool | — | Target klepstand (true = DHW) |
| `oq_purge_duration_min` | number | 3–30, default 5 | Totale duur |
| `oq_purge_remaining_s` | sensor | 0–1800 | Afteller |
| `oq_purge_phase_sensor` | sensor | 0/1/2/3 | Fase nummer |
| `oq_purge_phase_text` | text_sensor | string | Fase-omschrijving NL |

### Faseberekening (multi-cycle)

```cpp
elapsed_ms    = millis() - oq_cm97_active_since_ms
total_ms      = oq_purge_duration_min * 60 * 1000

cycle_ms      = total_ms / 3                  // 3 cycli
which_cycle   = elapsed_ms / cycle_ms         // 0, 1, of 2
cycle_elapsed = elapsed_ms - which_cycle * cycle_ms

settle_end    = cycle_ms / 5                  // 20%
cvpulse_end   = cycle_ms * 3 / 5              // 60%

if (cycle_elapsed < settle_end) {
  // Settle — pomp uit, valve CV
  phase = which_cycle*3 + 1
  ipwm = 1000
  valve = CV
}
else if (cycle_elapsed < cvpulse_end) {
  // CV pulse — 20s max / 10s stop
  phase = which_cycle*3 + 2
  ipwm = (in_pulse < 20s) ? 50 : 1000
  valve = CV
}
else {
  // DHW pulse — 20s max / 10s stop
  phase = which_cycle*3 + 3
  ipwm = (in_pulse < 20s) ? 50 : 1000
  valve = DHW
}
```

### Pulse pattern

In fase 2 en 3 wordt de pomp gepulseerd met cyclus van **40 seconden**:
- Eerste 30 s op hoge iPWM
- Daarna 10 s op lage iPWM
- Herhaalt

Aantal pulsen per fase = `120s / 40s = 3 pulsen`.

---

## Wijzigingsgeschiedenis

| Versie | Wijziging |
|--------|-----------|
| v0.40.0 | **Initial release** — CM97 ontluchtingsprotocol toegevoegd |
| v0.40.0 | **Multi-cycle upgrade** — 3 cycli met settle-pauzes (Henry's law), default duur 5 → 12 min |
