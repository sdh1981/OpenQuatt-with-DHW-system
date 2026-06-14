# Power House — Thermisch model v0.32

In firmware v0.32 zijn vijf concrete verbeteringen aan het thermische model van de Power House strategie toegevoegd. Alle zijn opt-in of zacht ingesteld zodat bestaande installaties geen gedragsverandering ondervinden tot je ze actief inschakelt.

## Overzicht

| # | Verbetering | Default | Tunbaar via |
|---|---|---|---|
| 1 | Self-learning UA-coëfficiënt | uit | `switch.openquatt_power_house_ua_learning_enable` |
| 2 | Zonnewinst-correctie | uit | `switch.openquatt_power_house_solar_gain_enable` |
| 3 | Voorspellend voorconditioneren | uit (gain=0) | `number.openquatt_power_house_preheat_gain` |
| 4 | Frost-zone derating | aan, mild | `number.openquatt_power_house_frost_zone_derate` |
| 5 | Effectieve aanvoertemperatuur voor perf_map | aan | `number.openquatt_power_house_design_supply_dt` |

---

## 1. Self-learning UA-coëfficiënt

**Wat:** een EMA-schatter die een specifieke warmtetransmissie-coëfficiënt UA (W/K) leert uit het werkelijk geleverde HP-vermogen versus `(Trsp − Tout)`.

**Waarom:** de drie huis-parameters in Power House (`house_rated_power_w`, `house_zero_power_temp_c`, `house_cold_temp_c`) bepalen direct of het huismodel klopt. Een verkeerde `Pr` zorgt structureel voor onder- of overshoot dat alleen door comfort_memory en `Kp·e` wordt opgevangen. De leer-routine biedt een datagedreven schatting.

**Hoe:**

1. Schatter is **opt-in** via `switch.openquatt_power_house_ua_learning_enable`. Default uit.
2. Tijdens stabiele periodes wordt elke 2 minuten een sample genomen:
   - `UA_sample = P_th_estimate / max(Trsp − Tout, 5K)`
   - `P_th_estimate` komt uit `interp_power_th_w(applied_level, Tamb, Tsup_meas)` per HP, met defrost-correctie als de 4-way-valve actief is.
3. Stabiliteits-condities (allemaal nodig):
   - kamertemperatuur binnen ±0,3 K van setpoint
   - geen DHW-cyclus (`oq_dhw_block_cv_priority` = false)
   - geen defrost en geen oil-return op beide HPs
   - HP levert ten minste 500 W thermisch
   - ΔT-load ≥ 5 K
4. Outlier-rejectie: als nieuwe sample >3× of <0,25× van de huidige EMA, sla over.
5. Eerste 6 samples: rekenkundig gemiddelde (snelle seed). Daarna EMA met `τ` = `number.openquatt_power_house_ua_learn_time_constant` (default 48 u, range 24–168 u).

**Diagnose-entiteiten:**
- `sensor.openquatt_power_house_ua_estimate` — actuele schatting (W/K)
- `sensor.openquatt_power_house_estimated_rated_power` — `UA × (T0 − Tc)` in W
- `sensor.openquatt_power_house_ua_samples` — aantal samples
- `sensor.openquatt_power_house_hp_thermal_output_estimate` — actueel geschat HP vermogen (W)

**Toepassen:**
- Druk op `button.openquatt_power_house_apply_ua_estimate` om de geleerde waarde over te nemen in `house_rated_power_w`. Veiligheid: alleen waardes tussen 1 000 en 15 000 W worden geaccepteerd.
- Druk op `button.openquatt_power_house_reset_ua_learning` om opnieuw te beginnen (na grote ingreep zoals isolatie).

**Wanneer aanzetten:**
- Na minimaal 1 stabiele week stookgedrag.
- Niet tijdens grote isolatie- of comfort-experimenten.
- Niet als je `house_rated_power_w` net op de proefondervindelijke ‘gouden’ waarde hebt staan — controleer eerst of de leer-schatting in dezelfde orde ligt.

---

## 2. Zonnewinst-correctie

**Wat:** een EMA over de over-shoot van de kamer in milde buitenomstandigheden, vertaald naar W en afgetrokken van `P_house`.

**Waarom:** op zonnige overgangsdagen onderschat het lineaire huismodel de externe winst → kamer 0,3-0,8 K boven setpoint, comfort_memory decayt te traag, Power House blijft te lang stoken.

**Hoe:**

1. Inschakelbaar via `switch.openquatt_power_house_solar_gain_enable`.
2. Instant-residual:
   - alleen actief bij `Tout > 5 °C` en `Tr − Trsp > 0,05 K`
   - `P_residual = Pr × (Tr − Trsp) / max(Trsp − Tc, 2)`, geclipd op `[0, 0,6·Pr]`
3. EMA met τ = 30 min:
   - `solar_gain += α·(P_residual − solar_gain)` waar `α = 1 − exp(−Δt/τ)`
4. Correctie op `P_house`:
   - `P_house_corrected = max(0, P_house − ph_solar_gain_alpha · solar_gain)`
   - `ph_solar_gain_alpha` default 0,7, range 0–1.

**Diagnose:** `sensor.openquatt_power_house_solar_gain_estimate` (W).

**Tuning:**
- Zie je nog steeds 0,5+ K over-shoot in maart/oktober: zet `ph_solar_gain_alpha` hoger (0,8–1,0).
- Lijkt de regeling te terughoudend op winter-zonneuren: zet alpha lager (0,4–0,5).
- Bij twijfel uit-staat = compleet veilig, geen risico op onder-stoken.

---

## 3. Voorspellend voorconditioneren

**Wat:** anticipatie op een opwaartse setback met laag compressorlevel — beste-COP-zone benutten in plaats van een spike bij setpoint-jump.

**Waarom:** standaard reageert PH pas wanneer `Tr` onder de band zakt. Bij een ochtend-setback van 18→20 °C moet de regeling dan met hoog level inhalen. De HP heeft de hoogste COP in level 4–6 (zie `hp_perf_map.h`); langer in dat venster draaien levert meer SCOP.

**Hoe:**

1. Twee optionele HA-proxy entiteiten:
   - `sensor.openquatt_ext_outdoor_temperature_forecast` — verwachte buitentemp 1-3 u vooruit
   - `sensor.openquatt_ext_room_setpoint_next` — kamersetpoint over een uur
2. Bij `Trsp_next > Trsp + 0,3 K`:
   - bereken `P_house_next` met de forecast-Tout
   - `P_preheat = ph_preheat_gain · 0,4 · P_house_next`
   - vloer `P_raw` op `P_preheat` zodat de strategie alvast zachtjes oploopt
3. Default `ph_preheat_gain = 0`, dus zonder configuratie geen effect.

**Setup in HA:**

```yaml
template:
  - sensor:
      - name: "OpenQuatt ext room setpoint next"
        unique_id: openquatt_ext_room_setpoint_next
        unit_of_measurement: "°C"
        state: >
          {% set next = state_attr('climate.woonkamer', 'next_target_temp') %}
          {{ next if next is not none else state_attr('climate.woonkamer', 'temperature') }}
```

Voor de buiten-forecast kan `weather.home` met `forecast_hourly[1].temperature` of `forecast_hourly[2].temperature` worden gebruikt.

**Beginnen met preheat:**
- Zet `ph_preheat_gain` op 0,3 voor mild effect.
- Kijk een week mee in `sensor.openquatt_power_house_p_req`.
- Verhoog naar 0,5 als de kamer te traag oploopt; verlaag naar 0,2 als de regeling te vroeg gaat stoken.

---

## 4. Frost-zone derating

**Wat:** een continue, milde derating van de geschatte HP-capaciteit rond +1,5 °C buiten — daar heeft de buitenunit de hoogste defrost-duty cycle door condensaatvorming.

**Waarom:** de bestaande `oq_defrost_power_factor` (0,55) werkt **alleen tijdens een actieve defrost**. Tussen defrost-cycli rekent de optimizer met volle capaciteit. In de natte vorst-zone verlies je gemiddeld 10-20 % capaciteit door frequente defrost. Zonder derating kiest de optimizer te laag level → onderlevering op +2 °C/95 %RH.

**Hoe:**

1. Continu actief, maar alleen wanneer **niet** in actieve defrost (4-way valve uit) en `Tamb` in `[−10, 6] °C`.
2. Bell-curve gecentreerd op +1,5 °C, σ = 3,5 K:
   - `derate(Tamb) = max_derate · exp(−((Tamb − 1,5) / 3,5)²)`
   - `factor = max(0,70, 1 − derate)`
3. `max_derate` user-tunable via `number.openquatt_power_house_frost_zone_derate`. Default 0,15 (max 15 % derating bij +1,5 °C).
4. Buiten de band of bij `max_derate = 0`: factor = 1,0 (geen effect).

**Default v0.32:** `0,15` — een conservatieve voorinstelling. Wil je strikt het oude gedrag: zet op `0`.

---

## 5. Effectieve aanvoertemperatuur voor perf_map

**Wat:** bij een koude start gebruikt de optimizer voortaan een afgeleide *steady-state* `Tsup` voor capaciteits-inschattingen, niet de live gemeten `Tsup`.

**Waarom (de bug):** `interp_power_th_w(level, Tamb, Tsup)` retourneert hogere W bij lagere `Tsup` (logisch: lagere supply = makkelijker voor de HP). Bij koude start is de gemeten supply rond 25 °C terwijl het doel 40-45 °C is. De perf_map zegt: "veel vermogen op laag level beschikbaar". De optimizer kiest een te laag level → langzaam opwarmen.

**Hoe:**

1. `Tsup_steady` afgeleid uit `P_target` en `house_rated_power_w`:
   - 0 % vraag → 30 °C
   - 100 % vraag → 55 °C
   - lineair daartussen
2. `Tsup_eff = max(Tsup_meas, Tsup_steady)` voor alle perf_map calls in de optimizer.
3. `number.openquatt_power_house_design_supply_dt` (default 5 K) is gereserveerd voor toekomstige flow-gebaseerde afleiding.

**Diagnose:** `sensor.openquatt_power_house_effective_supply_temp` toont de waarde die de optimizer daadwerkelijk gebruikt.

**Wat verandert er voor jou:**
- Kortere koude-start fase, level loopt sneller op naar passend niveau.
- Geen invloed in steady-state — daar is `Tsup_meas ≥ Tsup_steady` vanzelfsprekend.
- `Tsup_meas` blijft de bron voor de UA-leer-schatter (#1) — daar wíl je juist de werkelijkheid.

---

## Volgorde van inschakelen (aanbevolen)

1. **Update naar v0.32** — alle features behalve #4 staan dan nog uit, geen gedragsverandering.
2. **#5** is direct actief — koude-start moet duidelijk sneller voelen. Als alles werkt: door.
3. **Zet #2 aan** (`solar_gain_enable`). Een week meekijken op zonnige dagen.
4. **Zet #1 aan** (`ua_learning_enable`). Wacht ~1 week; check `ua_samples` ≥ 30, dan `apply_ua_estimate` als de waarde plausibel is.
5. **Optioneel #3**: alleen als je een variabel kamersetpoint hebt en de HA-proxy entiteiten geconfigureerd zijn.
6. **#4** kun je tunen: 0 = oud gedrag, 0,15 = default, 0,25 = agressief.

## Veiligheid en safe-defaults

- Geen van de features kan het oude gedrag negatief beïnvloeden zolang ze uit/op default staan.
- UA-leer en zonnewinst hebben outlier-rejectie en bounds.
- De apply-knop voor UA accepteert alleen 1 000-15 000 W, voorkomt onbedoelde extreme waardes.
- Reset-knop voor UA: gewist na isolatie of grote configuratie-aanpassing.
