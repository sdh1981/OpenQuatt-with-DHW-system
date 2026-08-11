# OpenQuatt with DHW system

<img src="docs/assets/openquatt_logo.svg" alt="OpenQuatt logo" width="400" />

ESPHome-firmware voor een **Quatt Duo** met een zelfgebouwd warmwatersysteem erbij: een 220 L boiler met eigen spiraal, driewegklep, 3 kW element en legionellaprogramma, aangestuurd vanaf dezelfde controller die de warmtepompen regelt.

Dit is een fork van [OpenQuatt van Jeroen85](https://github.com/Jeroen85/OpenQuatt). Dat project is de basis: de Modbus-aansturing van de Quatt-hardware, de supervisory en de strategie-opzet komen daarvandaan. Wat hier bovenop zit — de hele DHW-keten, de CiC-brug, de beveiligingslagen en de Energy OS-koppeling — is voor één specifieke installatie gebouwd.

> [!WARNING]
> Experimenteel, en toegespitst op één opstelling. Gebruik van OpenQuatt kan gevolgen hebben voor je Quatt-garantie: de commerciële garantie vervalt in principe bij externe aansturing. De wettelijke garantie blijft, maar een claim wordt in de praktijk lastiger.
>
> Zoek je de algemene, breed ondersteunde versie voor Single- én Duo-opstellingen op meerdere hardwareprofielen, ga dan naar het [bovenliggende project](https://github.com/Jeroen85/OpenQuatt).

## De installatie waar dit voor gebouwd is

| Onderdeel | Uitvoering |
|---|---|
| Warmtepomp | Quatt Duo, 2× ODU |
| Controller | LilyGO T-Connect (ESP32-S3) |
| Boiler | Inventum horizontaal 220 L |
| Driewegklep | Honeywell V4044, veerretour, met aux-contact als feedback |
| Boostverwarming | 3 kW element via contactor |
| Tanksensoren | CWT-TM-8PT, 8-kanaals PT1000 op RS485 #3 |

**Fasebedrading:** HP1 en HP2 staan samen op L1, het 3 kW element op L2. Beide tegelijk is daardoor geen overbelasting van één groep. De 16 A-supervisory bewaakt alleen het HP-vermogen op L1.

Volledige pinmapping en bekabeling: [Hardwarebeschrijving Duo + DHW](docs/hardware-dhw-lilygo.md).

## Builds

Twee configuraties, allebei gebouwd door CI:

| Config | Wat |
|---|---|
| `openquatt_duo_lilygo_tconnect.yaml` | Duo + DHW |
| `openquatt_duo_lilygo_tconnect+cic.yaml` | idem, plus CiC-brug |

De `+CIC`-build laat de Quatt CiC in het systeem zitten: OpenQuatt gedraagt zich als Modbus-slave richting de CiC en vertaalt waar nodig, zodat de fabrieksregelaar zijn eigen beeld houdt.

De hardwareprofielen voor Waveshare en Heatpump Listener staan nog in `openquatt/profiles/`, maar er is voor deze fork geen build-config voor en ze worden niet getest.

Compileren en flashen:

```bash
python -m esphome run openquatt_duo_lilygo_tconnect+cic.yaml
```

## Wat deze fork toevoegt

### Warmwater

Een complete toestandsmachine — `IDLE_CV → DHW_PREPARE → DHW_HEAT_PUMP → DHW_BOOST`, met legionella en foutafhandeling ernaast. Zie [DHW-instellingen](docs/dhw-instellingen.md).

- **Single-HP mode** met vastgezette lead per cyclus en gezondheidsafweging tussen de units, zodat de vraag niet halverwege naar de andere compressor verhuist.
- **Stapsgewijze tweede-HP assist**, die pas inschakelt als de tanktop aantoonbaar te traag stijgt, met eigen persgas- en water-uitbewaking.
- **Zachte aanloop** van het compressorniveau bij de start van de HP-fase.
- **Handmatige snelboost**: beide HP's plus het element, met eigen grenzen en afbreekbaar.
- **Legionellaprogramma** met pasteurisatie-hold, slimme uitstelregels en een plafond op de tanktop tijdens de HP-fase.

### Beveiliging

Per HP een cap op het compressorniveau, uit drie onafhankelijke bronnen. De strengste wint, en elke bron regelt eerst stapsgewijs terug voordat er gestopt wordt.

| Bron | Zacht | Hard |
|---|---|---|
| [Condensordruk](docs/pressure-protection-v0.32.md) | 38 bar | 40 bar |
| [Aanvoertemperatuur](docs/supply-temp-protection-v0.32.md) | 55 °C | 58 °C |
| [Persgastemperatuur](docs/discharge-protection-v0.52.md) | 98 °C | 105 °C |

### Verwarmen

Drie strategieën naast elkaar — zie [Heating Strategy](docs/heating-strategy.md):

- [Power House](docs/power-house.md), met huismodel, UA-leerroutine en zonnewinstcorrectie
- [Water Temperature Control](docs/water-temperature-control.md), stooklijn met PID
- [Adaptive Heating](docs/adaptive-heating.md), zelflerende offset op de stooklijn die de laagst mogelijke aanvoertemperatuur opzoekt

Plus twee bijzondere modes: [Element Only (CM6)](docs/element-only-heating-cm6-v0.40.md) en het [ontluchtingsprotocol (CM97)](docs/ontluchtingsprotocol-cm97-v0.40.md).

### Energy OS

De `+CIC`-build koppelt aan [Energy OS](https://github.com/sdh1981/energy-os) voor gecombineerde warmtepomp- en batterijsturing.

| Functie | Detail |
|---|---|
| HP cap extern schrijven | Energy OS throttelt via `number.openquatt_eos_hp_cap_ha` (0–20). Werkt alleen naar beneden; de eigen supervisory houdt altijd voorrang. |
| Comfort floor | Een buitentemperatuurafhankelijk minimum, zodat de cap comfort niet wegregelt. |
| PV-voorrang | Leveren de panelen meer dan de HP verbruikt, dan geeft Energy OS de HP vrij ongeacht de batterijmodus. |
| DHW-vensters | De goedkoopste en duurste tariefvensters gaan naar HA-datetimes die OpenQuatt leest voor legionella en opwarming. |

Default is 20, dus zonder Energy OS doet de brug niets. De brug kan de cap nooit verhogen.

Het package `openquatt/oq_energy_os_bridge.yaml` zit al in de `+CIC`-config. Na het flashen moet in Energy OS het commentaar weg bij `number.set_value` in `script.eos_apply_hp_cap` (`eos_03_dispatcher.yaml`).

## Dashboard

Voor deze opstelling worden twee dashboards onderhouden, allebei Nederlands en Duo:

- [openquatt_ha_dashboard_duo_nl.yaml](docs/dashboard/openquatt_ha_dashboard_duo_nl.yaml)
- [openquatt_ha_dashboard_duo_nl_v2.yaml](docs/dashboard/openquatt_ha_dashboard_duo_nl_v2.yaml)

De Engelse en Single-varianten in dezelfde map komen uit het bovenliggende project en lopen achter; ze missen alles wat hier na juli is bijgekomen.

Importeren: [Dashboard installeren](docs/dashboard/README.md). Wat je op welke tab vindt: [Dashboardoverzicht](docs/dashboardoverzicht.md).

## Documentatie

Begin bij het [documentatie-overzicht](docs/README.md). Voor dagelijks gebruik:

- [Hoe OpenQuatt werkt](docs/hoe-openquatt-werkt.md) — rolverdeling tussen thermostaat, OpenQuatt, warmtepomp en Home Assistant
- [Diagnose en afstelling](docs/diagnose-en-afstelling.md) — werkvolgorde bij afwijkend gedrag
- [DHW-instellingen](docs/dhw-instellingen.md) — alle warmwaterparameters
- [Koeling-instellingen](docs/koeling-instellingen.md) — dauwpuntbeveiliging, nacht-free cooling, emitterprofiel
- [Instellingen en meetwaarden](docs/instellingen-en-meetwaarden.md) — compile-time en runtime

## Licentie

GPL-3.0, overgenomen van het bovenliggende project. Zie [LICENSE](LICENSE).
