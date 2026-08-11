# Persgasbeveiliging (v0.52)

Per-HP begrenzing van het compressorniveau op basis van de gemeten persgastemperatuur. Voor R32 is persgas de scherpste vroege indicator van een compressor die te hard werkt — scherper dan condensordruk, omdat de druk pas oploopt als de warmte er al is.

## Waarom deze module er is

Tot v0.52 bestond er maar één persgasgrens: `DHW assist max discharge temp` (90 °C). Die beslist uitsluitend of de **tweede** unit mag bijschakelen. Voor de unit die al draaide was er geen enkele terugregeling op persgas.

Dat is precies het gat dat de hogedruktrip tijdens legionella liet zien: de compressor liep door tot de druk of de ODU zelf ingreep.

## De vier trappen samen

| Grens | Wat er gebeurt | Waar |
|---|---|---|
| **90 °C** | tweede unit mag niet bijschakelen | assist-guard, `oq_boiler_control.yaml` |
| **98 °C** | draaiende unit knijpt af, stap voor stap | deze module, zachte trap |
| **105 °C** | draaiende unit stopt | deze module, harde trap |
| **110 °C** | eigen beveiliging van de ODU | fabrikant |

OpenQuatt grijpt dus aantoonbaar eerder in dan de ODU; die blijft het vangnet.

De scheiding tussen 90 en 98 is bewust. 90 °C is een voorzorg — geen tweede compressor erbij. 98 °C is een ingreep — de unit die draait moet minder. Voorheen moest die ene grens van 90 °C beide rollen dekken.

## Tier-gedrag per HP

Op basis van `hp1_gas_discharge_temp` / `hp2_gas_discharge_temp`:

| Tier | Drempel | Level cap | Wat gebeurt |
|---|---|---|---|
| 0 (OK) | < soft_c | 10 (geen cap) | Normaal bedrijf |
| 1 (soft) | soft_c..hard_c | ladder | Niveau stapsgewijs omlaag |
| 2 (hard) | >= hard_c | 0 (stop) | Compressor stopt |

Een ontbrekende of onzinnige meting (NaN, of ≤ 0 °C) levert altijd tier 0. Die kant op fout gaan zou de compressor stilzetten op een sensor die toevallig even niets teruggeeft.

## De zachte trap

Identiek aan de ladder in [supply-temp-protection](supply-temp-protection-v0.32.md):

1. **Bij binnenkomst** eenmalig latchen op `laatst toegepast niveau − reduce`, ondergrens 1.
2. **Daarna** per `Discharge soft cap step time` (default 120 s) nog een level eraf, maar alleen zolang de persgastemperatuur nog stijgt.
3. **Bij een harde trip** blijft de ladder gelatcht.
4. **Vrijgeven** alleen bij terugval naar tier 0, en die wordt door de hysterese al tegengehouden.

## Samenspel met de andere caps

De thermal actuator combineert alles via `min()`:

```
final_cap = min(silent/day_cap, pressure_cap, supply_temp_cap, discharge_cap)
```

Plus de level-exclusies. De strengste wint.

## Configuratie

| Entiteit | Default | Range | Effect |
|---|---|---|---|
| `Discharge protection enable` | **aan** | aan/uit | Master switch |
| `Discharge soft cap` | 98 °C | 80-105 | Drempel voor de ladder |
| `Discharge hard cap` | 105 °C | 90-112 | Drempel voor stop |
| `Discharge release hysteresis` | 5 K | 1-15 | Daling nodig voor cap-release |
| `Discharge soft cap reduce levels` | 1 | 1-4 | Eerste stap onder het draaiende niveau |
| `Discharge soft cap step time` | 120 s | 0-600 | Tijd per vervolgstap; 0 = alleen de eerste |

## Diagnose

- `sensor.openquatt_discharge_hp1_level_cap` / `hp2` — actieve cap, 10 = geen begrenzing
- `sensor.openquatt_discharge_hp1_peak` / `hp2` — hoogste gemeten persgastemperatuur sinds de laatste reset. Een stijgende piek bij gelijke omstandigheden wijst op minder koeling van de compressor.
- `sensor.openquatt_discharge_soft_events_24h` / `hard` — hoe vaak per trap
- `binary_sensor.openquatt_discharge_soft_cap_active` / `hard` — actief nu
- `button.openquatt_discharge_reset_event_counters` — tellers én pieken op nul

Alles staat samen op de sectie **Compressorbegrenzing** van de diagnostiektab.

## Afstellen

Vuurt de zachte trap tientallen keren per dag, dan staat `Discharge soft cap` te laag voor jouw installatie — niet dat de beveiliging te streng is. Kijk dan eerst naar de piek-sensoren: blijft de piek ruim onder 105 °C, dan doet de ladder zijn werk en kun je de zachte grens een paar graden omhoog.

Zie je de harde trap vuren, dan is dat een signaal op zich. Dan haalt de zachte trap het niet, en is `soft cap step time` te lang of `reduce levels` te klein.
