# Smart Features v0.32

Naast de vijf [thermisch-model verbeteringen](power-house-thermisch-model-v0.32.md) introduceert v0.32 zeven extra slimme features. Vier daarvan grijpen actief in op de Power House regeling, drie zijn observatie-features (SCOP, health, time-to-setpoint).

## Overzicht

| # | Feature | Type | Default | Vereist HA-input |
|---|---|---|---|---|
| 1 | Dynamisch tarief-aware setpoint shift | actief | uit | ja, dynamisch tarief |
| 2 | PV-zelfconsumptie boost | actief | uit | ja, grid net power |
| 3 | Window-open detectie | actief | uit | nee |
| 4 | Adaptieve Kp observer | leer | uit | nee |
| 6 | SCOP rolling tracking | observatie | aan | nee |
| 7 | Compressor-health monitoring | observatie | aan | nee |
| 8 | Heat-up rate / time-to-setpoint | observatie | aan | nee |

---

## 1. Dynamisch tarief-aware setpoint shift

**Wat:** als je een dynamisch elektriciteitstarief hebt (Tibber, ANWB, EPEX-spotprijs), schuift OpenQuatt het effectieve setpoint stilletjes op basis van de prijs:

- **Goedkoop uur** (`tarief_nu < cheap_ratio × 24h_gemiddelde`): zet setpoint tot `+max_up` °C — preheat met goedkope stroom in thermische massa.
- **Duur uur** (`tarief_nu > expensive_ratio × 24h_gemiddelde`): zet setpoint tot `-max_down` °C — throttle naar comfort-ondergrens.

De shift wordt vertaald naar extra/minder Watts via `Kp · shift`, zodat het naadloos in het Power House power-pad werkt. Slewing op 0,05 K/min voorkomt whipping bij prijssprongen.

**Tuning entiteiten:**
- `switch.openquatt_power_house_tariff_shift_enable` — aan/uit
- `number.openquatt_power_house_tariff_preheat_max` — max +shift bij dalbodem (default 0,5 °C)
- `number.openquatt_power_house_tariff_throttle_max` — max -shift bij piek (default 0,3 °C)
- `number.openquatt_power_house_tariff_cheap_ratio` — drempel goedkoop (default 0,7)
- `number.openquatt_power_house_tariff_expensive_ratio` — drempel duur (default 1,3)

**HA-setup voorbeeld (Tibber):**
```yaml
template:
  - sensor:
      - name: "OpenQuatt ext electricity tariff now"
        unique_id: openquatt_ext_electricity_tariff_now
        unit_of_measurement: "EUR/kWh"
        state: "{{ state_attr('sensor.tibber_prices', 'today')[now().hour]['total'] }}"
      - name: "OpenQuatt ext electricity tariff avg24"
        unique_id: openquatt_ext_electricity_tariff_avg24
        unit_of_measurement: "EUR/kWh"
        state: >
          {% set today = state_attr('sensor.tibber_prices', 'today') %}
          {% set total = today | map(attribute='total') | list %}
          {{ (total | sum / total | length) | round(4) }}
```

**Diagnose:**
- `sensor.openquatt_power_house_tariff_setpoint_shift` — actueel toegepaste shift
- `sensor.openquatt_power_house_tariff_ratio` — tarief / 24h-gemiddelde

**Veiligheidsnet:** als HA-sensor faalt (`unavailable`), wordt geen shift toegepast. Eigen running average als fallback voor het 24h-gemiddelde.

---

## 2. PV-zelfconsumptie boost

**Wat:** bij PV-overproductie (`grid_net_power < -drempel`) verhoogt OpenQuatt `P_target` om die teruglevering om te zetten in warmte. De berekening:

```
boost_W = min(boost_max, (export_W - drempel_W) × assumed_COP)
```

De aanname `assumed_COP` (default 3,5) is een redelijk gemiddelde. Als de werkelijke COP hoger is win je extra; lager: je zet wat meer huidige stroom in.

**Tuning entiteiten:**
- `switch.openquatt_power_house_pv_self_consumption_enable`
- `number.openquatt_power_house_pv_export_threshold` — drempel-export voor activatie (default 800 W)
- `number.openquatt_power_house_pv_boost_max` — max boost (default 2000 W)
- `number.openquatt_power_house_pv_assumed_cop` — aanname (default 3,5)

**HA-setup:**
- `sensor.openquatt_ext_grid_net_power` — netto vermogen (negatief = export). Combineer P1-meter teruglever- en afnamesensor:

```yaml
template:
  - sensor:
      - name: "OpenQuatt ext grid net power"
        unique_id: openquatt_ext_grid_net_power
        unit_of_measurement: "W"
        device_class: power
        state: >
          {% set imp = states('sensor.p1_meter_active_power_total') | float(0) %}
          {{ imp }}
```

(`p1_meter_active_power_total` is doorgaans positief = import, negatief = export.)

**Diagnose:** `sensor.openquatt_power_house_pv_boost` — actueel toegepaste boost in W.

**Slewing:** 200 W/s rise/fall, dus boost reageert snel op zonnewolken zonder te whipperen.

---

## 3. Window-open detectie

**Wat:** detecteert een snelle daling van kamertemperatuur (>0,35 K binnen 5 minuten) terwijl het buiten kouder is dan binnen. Triggert een pauze van 12 minuten waarin `P_effective = 0`.

**Hoe:**
- 5-minuten rolling snapshot van `room_temp_selected`
- Drop-detectie alleen actief als `Tout < Tr − 1 K` (anders niet plausibel een open raam)
- Auto-clear na pauze-window

**Tuning:**
- `switch.openquatt_power_house_window_open_detection`
- `number.openquatt_power_house_window_open_temp_drop` — drempel (default 0,35 K)
- `number.openquatt_power_house_window_open_pause` — pauze (default 12 min)

**Diagnose:**
- `binary_sensor.openquatt_power_house_window_open_active` — actief tijdens pauze

**Geen externe inputs nodig** — werkt alleen op bestaande `room_temp_selected`.

**False-positives:** als je je raam regelmatig kort opent en sluit, kan de detectie kortstondig triggeren. Zet desnoods `temp_drop` op 0,5 K of pause op 8 min.

---

## 4. Adaptieve Kp observer

**Wat:** observeert de werkelijke verhouding `(P_raw − P_house) / |error|` tijdens stooktijd en bouwt een EMA-schatting op. Helpt je om `ph_kp_w_per_k` te kalibreren als de standaard 3000 W/K niet bij jouw huis past.

**Hoe:**
- Sampling-condities: `|Tr − Trsp| > 0,3 K`, dt ≥ 60 s, P_extra > 0
- EMA met τ = 72 u (default, 24-168 u tunbaar)
- Outlier-rejectie: <0,25× of >4× huidige EMA → skip
- Bounds: 200..8000 W/K

**Toepassen:**
- `button.openquatt_power_house_apply_kp_estimate` — schrijft schatting naar `ph_kp_w_per_k`
- `button.openquatt_power_house_reset_kp_learning` — wist leerstaat

**Diagnose:**
- `sensor.openquatt_power_house_adaptive_kp_estimate` — schatting in W/K
- `sensor.openquatt_power_house_kp_samples` — sample-aantal

**Wanneer toepassen:** wacht tot ≥30 samples en zie of de schatting stabiel zit ±10%. Wijkt het sterk af van je huidige `ph_kp_w_per_k`, dan is het zinvol om aan te passen.

---

## 6. SCOP rolling tracking

**Wat:** lopend gemiddelde van seizoens-COP (`heat_out / energy_in`) over vier rolling vensters: 24 u, 7 d, 30 d en lifetime.

**Wat is SCOP?** Geen instantane COP maar geïntegreerd over tijd. Rekent realistische verhouding tussen geleverde warmte en gebruikte stroom.

**Wat wordt geïntegreerd:**
- `oq_total_power_input` (W) → input
- `oq_total_heat_power` (W) → output
- Alleen bij meaningful heating (`p_out > 50 W` AND `p_in > 30 W`) — voorkomt dat standby-vermogen het getal verstoort.

**Sensors:**
- `sensor.openquatt_scop_24h` — afgelopen 24 u
- `sensor.openquatt_scop_7d`
- `sensor.openquatt_scop_30d`
- `sensor.openquatt_scop_lifetime` — sinds laatste reset
- `sensor.openquatt_energy_in_24h` / `sensor.openquatt_heat_out_24h` — energie balansen

**Knoppen:**
- `button.openquatt_scop_reset_rolling_windows` — wist 24h/7d/30d
- `button.openquatt_scop_reset_lifetime` — wist totaal (gebruik 1× bij begin van seizoen)

**Persistentie:** alle accumulators zijn `restore_value: true` — overleeft reboots. SCOP-getallen blijven dus consistent over firmware updates.

**Initialisatie:** SCOP wordt pas getoond als 24h ≥50 Wh, 7d ≥100 Wh, 30d ≥500 Wh, lifetime ≥1000 Wh. Daaronder = NaN (geen waarde).

---

## 7. Compressor-health monitoring

**Wat:** drie health-indicatoren die anomalieën signaleren:

### Short cycles per 24h
Aantal HP-runs korter dan 5 minuten. Een paar per dag is normaal (bv. randgebieden van comfort-band). Meer dan 8/dag = mogelijk te krappe comfort-band, verkeerde rated power, of cycle-discipline issue.

- `sensor.openquatt_health_short_cycles_24h`
- `binary_sensor.openquatt_health_short_cycle_warning` — actief bij ≥8

### Defrosts per 24h
Aantal rising-edges van 4-way valve. Bij +1...+3 °C met hoge RH is 10-20 normaal. >24 = mogelijk frost-build issue (vuile buitenunit, te lage flow).

- `sensor.openquatt_health_defrost_cycles_24h`
- `binary_sensor.openquatt_health_high_defrost_warning` — actief bij ≥24

### COP deviation
EMA (τ = 6 u) van het verschil tussen gemeten COP (`heat / electric`) en perf_map COP. Verwacht: rond 0. Persistent <-0,4 = onderprestatie (vuil filter, koelmiddel-issue, sensor drift). Persistent >+0,4 = overschatting van perf_map of foute meting.

- `sensor.openquatt_health_cop_deviation`
- `binary_sensor.openquatt_health_cop_anomaly` — actief bij |dev| > 0,4

**Reset:** `button.openquatt_health_counters_reset`. 24h-counters resetten automatisch elke 24 u.

---

## 8. Heat-up rate sensor

**Wat:** voorspelt hoe lang het nog duurt tot de kamer de setpoint bereikt.

**Hoe:**
- Sample dTr/dt elke ≥60 s, EMA τ = 10 min
- `tijd_tot_setpoint = (Trsp - Tr) / heatup_rate × 60 min`
- NaN als rate ≤0,05 K/u (niet noemenswaardig opwarmend)

**Sensors:**
- `sensor.openquatt_room_heat_up_rate` — actuele rate (K/u)
- `sensor.openquatt_estimated_time_to_setpoint` — voorspelde minuten tot SP

**Use cases:**
- Dashboard-tegel: "Setpoint over ~22 min"
- HA-automation: pre-bericht aan smart home als het zo lang duurt
- Vergelijking voor/na isolatie of afstellingen

---

## Aanbevolen volgorde van inschakelen

1. **#6, #7, #8 staan automatisch aan** — observatie, geen impact op gedrag.
2. **#3 Window-detectie** — geen extra inputs, lage risk. Aanzetten en evalueren.
3. **#1 Dynamisch tarief** — alleen zinvol bij dynamisch contract. Configureer eerst de HA-proxy entiteiten, dan switch aan.
4. **#2 PV-boost** — alleen bij PV. Configureer P1-proxy, dan aan.
5. **#4 Adaptieve Kp** — laat een week+ leren voor je apply gebruikt. Werkt het beste in combinatie met UA-leren (#1 in v0.32 thermisch model).

## Veiligheid

- Alle actieve features (#1, #2, #3) staan default uit.
- `#3` heeft een hard window cap (`pause` max 30 min), zodat het niet onbeperkt blijft pauzeren.
- `#1` slewt setpoint-shifts op 0,05 K/min — voorkomt comfort-glitches.
- `#2` slewt PV-boost op 200 W/s — voorkomt level-jumping bij wolken.
- Alle observatie-features (#6, #7, #8) hebben geen invloed op control output.
