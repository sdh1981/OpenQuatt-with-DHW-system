# Koeling-instellingen

Deze pagina beschrijft alle instelbare parameters van de koelstrategie in OpenQuatt. Koeling is alleen actief in CM5 (control mode 5). De regeling gebruikt een PI-regelaar op aanvoertemperatuur, met dauwpuntbeveiliging en optionele dual-HP-ondersteuning.

## Overzicht regelpad

```
Buitentemperatuur + emitterprofiel
  │
  ▼
Aanvoertemperatuur doel (cooling_supply_target)
  │
  ▼
PI-regelaar: fout = aanvoer_doel − werkelijke_aanvoer
  │
  ▼
Vraag (0–10) ──► Minimale runtime ──► Auto dual-HP ──► HP1 / HP2 aanvraag
  │
  └─ Dauwpuntbeveiliging: aanvoer nooit onder dauwpunt + veiligheidsmarge
```

De strategietick en de PI-loop draaien in aparte intervallen, zodat de PI-berekening sneller is dan de actuele HP-aanvraag.

## Emitterprofiel

### Koeling emitterprofiel

- Entity: `select.openquatt_cooling_emitter_profile`
- Opties: **Radiatoren**, **Vloerkoeling**

Het profiel bepaalt de basisinstellingen voor meerdere koelparameters tegelijk. Na het kiezen van een profiel worden de volgende instellingen automatisch aangepast:

| Parameter | Radiatoren | Vloerkoeling |
|---|---|---|
| Minimum aanvoer | 12 °C | 18 °C |
| Comfort offset | 0 °C | 0 °C |
| Veiligheidsmarge | 1,8 °C | 2,2 °C |
| Minimale runtime | 480 s | 900 s |
| Auto dual-HP drempel | 3 | 2 |

Pas het profiel aan als je van emitter wisselt. Individuele parameters kunnen daarna nog handmatig bijgesteld worden.

## Aanvoertemperatuur

### Koeling aanvoer minimum

- Entity: `number.openquatt_cooling_supply_minimum`
- Standaard: afhankelijk van emitterprofiel (12 of 18 °C)
- Bereik: 10–22 °C

De laagste aanvoertemperatuur die de PI-regelaar mag aanvragen. Dit is een harde bodemgrens bovenop de dauwpuntbeveiliging. De werkelijk gehanteerde ondergrens is altijd de hoogste van: dit minimum, het dauwpunt + veiligheidsmarge.

### Koeling comfort offset

- Entity: `number.openquatt_cooling_comfort_offset`
- Standaard: **0 °C**
- Bereik: −3–3 °C

Verschuift het aanvoertemperatuur-doel omhoog of omlaag. Positieve waarden maken de koeling minder agressief (hogere aanvoer = minder koelen). Negatieve waarden koelen intensiever.

## Dauwpuntbeveiliging

De beveiliging berekent continu een minimaal veilige aanvoertemperatuur op basis van het dauwpunt van de ruimte. De aanvoer daalt nooit onder dit niveau om condensvorming op leidingen en emitters te voorkomen.

### Koeling veiligheidsmarge

- Entity: `number.openquatt_cooling_safety_margin`
- Standaard: afhankelijk van profiel (1,8 of 2,2 °C)
- Bereik: 0–4 °C

De vaste buffer bovenop het dauwpunt. De werkelijk gehanteerde marge wordt dynamisch verhoogd als het dauwpunt snel stijgt (zie dauwpunt-trend hieronder).

### Dauwpunt-trend en dynamische marge

De regeling berekent elke 15 minuten de stijgsnelheid van het dauwpunt in °C per uur. Stijgt het dauwpunt snel, dan wordt de veiligheidsmarge automatisch vergroot:

| Stijgsnelheid | Extra marge |
|---|---|
| < 0,3 °C/u | 0 °C extra |
| 0,3–0,8 °C/u | +0,3 °C |
| > 0,8 °C/u | +0,6 °C |
| maximum | 4,5 °C totaal |

Diagnostiek:
- `sensor.openquatt_cooling_dew_point_trend` — dauwpunt-stijgsnelheid (°C/h)
- `sensor.openquatt_cooling_dynamic_safety_margin` — actuele totale marge
- `sensor.openquatt_cooling_effective_minimum_supply_temp` — werkelijke ondergrens aanvoer

## Nachtkoelingvrij (free cooling)

Als de buitentemperatuur laag genoeg is, kan nachtventilatie effectiever zijn dan mechanische koeling. De "Cooling Night Free Cooling Active" sensor geeft aan wanneer deze conditie actief is.

### Drempel nachtkoelingvrij

- Entity: `number.openquatt_cooling_night_free_cooling_threshold`
- Standaard: **20 °C**
- Bereik: 12–24 °C (stap 0,5)

Als de buitentemperatuur onder deze drempel daalt, de ruimtetemperatuur niet meer dan 1,2 °C boven setpoint ligt, en de buitentemperatuur ook onder ruimtesetpoint − 0,5 °C zit, wordt de koelingstrategie geblokkeerd (stop-reden: `night_free_cooling`). De gedachte: als het buiten koel genoeg is, open je een raam in plaats van de warmtepomp te laten koelen.

Zet dit lager dan je setpoint (bijv. setpoint 22 °C → drempel 20 °C) zodat free cooling alleen actief is als het buiten werkelijk koel is.

## Anti-pendel (minimale looptijd)

Er is geen aparte `Cooling Minimum Runtime`-knop meer. Kortcyclus-bescherming loopt
nu via het **minimum niveau 1 in CM5**: zodra de warmtepomp voor koeling draait,
zakt hij niet volledig naar 0 maar blijft op niveau 1 tot de vraag terugkeert of
een harde stop optreedt (koeling uit, veiligheids-trip of DHW-conflict). Zo pendelt
de compressor niet bij lage koelvraag, zonder dat je een looptijd hoeft in te stellen.

## Dual-HP koeling (alleen Duo)

Bij een Duo-opstelling kan een tweede warmtepomp bijspringen als de koelvraag langdurig hoog is.

### Handmatige HP2-assist

- Entity: `number.openquatt_cooling_hp2_assist_level`
- Standaard: **0** (uit)
- Bereik: 0–10

Stel dit in op een waarde > 0 om de tweede warmtepomp handmatig te laten meekoelen op het opgegeven niveau. Niveau 0 betekent dat HP2 alleen bijspringt als de auto dual-HP-functie dit activeert.

### Auto dual-HP vraagdrempel

- Entity: `number.openquatt_cooling_auto_dual_hp_demand_threshold`
- Standaard: afhankelijk van profiel (3 voor radiatoren, 2 voor vloer)
- Bereik: 1–10 (stap 1)

De minimale PI-vraag (in stappen van 1–10) die 10 minuten aanhoudend bereikt moet worden voordat de tweede warmtepomp automatisch ingeschakeld wordt. Een lagere drempel laat HP2 eerder bijspringen.

Na het dalen van de vraag onder de drempel wordt de 10-minutentimer gereset. HP2 schakelt pas uit als de totale vraag naar 0 daalt.

## DHW-conflict

Tijdens een actieve DHW-cyclus (warmtepomp verwarmt tapwater) stopt de koelstrategie onmiddellijk:
- PI-integraal wordt gereset
- Vraag wordt op 0 gezet
- Minimale runtime wordt afgebroken
- Stop-reden: `dhw_active`

Zodra de DHW-cyclus eindigt, hervat de koelstrategie automatisch.

## Stop-redenen

De sensor `sensor.openquatt_cooling_stop_reason` geeft aan waarom er niet gekoeld wordt:

| Code | Tekst | Betekenis |
|---|---|---|
| 0 | *(leeg)* | Koeling actief |
| 6 | `demand_max_zero` | Maximum vraag ingesteld op 0 |
| 7 | `safe_floor_trip` | Aanvoer te dicht bij dauwpuntgrens |
| 8 | `cm_not_5` | Niet in CM5 (koelmodus) |
| 9 | `no_hp_available` | Geen warmtepomp beschikbaar om te starten |
| 10 | `dhw_active` | DHW-cyclus actief |
| 11 | `night_free_cooling` | Nachtvrije koeling actief |

## PI-regelaar

De koelstrategie gebruikt een PI-regelaar (proportioneel + integraal) op het verschil tussen gewenste en werkelijke aanvoertemperatuur. De regelaar heeft de volgende bescherming ingebouwd:

- **Anti-windup**: de integraal groeit niet verder als de output al op het maximum zit.
- **Integraal-reset**: bij negatieve fout (aanvoer al koeler dan doel) wordt de integraal direct op 0 gezet.
- **Simmer-logica**: bij kleine positieve fout en onvoldoende koelvraag houdt de regelaar niveau 1 vast in plaats van naar 0 te vallen. Dit voorkomt aan-uitschakelen rond de doeltemperatuur.

## Buitentemperatuurcorrectie

Bij hoge buitentemperatuur (> 28 °C) wordt het aanvoerdoel automatisch iets verlaagd (max. 1,5 °C bij 38 °C buiten). Dit compenseert voor de hogere warmtebelasting zonder dat de gebruiker iets hoeft bij te stellen.

## Diagnostiek

| Entiteit | Inhoud |
|---|---|
| `sensor.openquatt_cooling_supply_target` | Berekend aanvoertemperatuur-doel |
| `sensor.openquatt_cooling_effective_minimum_supply_temp` | Werkelijke ondergrens (dauwpunt + marge) |
| `sensor.openquatt_cooling_dynamic_safety_margin` | Actuele veiligheidsmarge inclusief trend-aanpassing |
| `sensor.openquatt_cooling_dew_point_trend` | Stijgsnelheid dauwpunt (°C/h) |
| `sensor.openquatt_cooling_stop_reason` | Reden waarom koeling niet actief is |
| `binary_sensor.openquatt_cooling_night_free_cooling_active` | Nachtvrije koeling actief |
| `sensor.openquatt_cooling_demand_raw` | PI-uitgangsvraag (0–10) vóór begrenzing |
| `sensor.openquatt_cooling_request_hp1_level` | Aangevraagd niveau HP1 |
| `sensor.openquatt_cooling_request_hp2_level` | Aangevraagd niveau HP2 |
