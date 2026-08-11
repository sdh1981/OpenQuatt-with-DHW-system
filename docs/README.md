# OpenQuatt documentatie

Deze documentatie is bedoeld voor gebruikers die OpenQuatt willen installeren, koppelen aan Home Assistant en daarna gericht willen afstellen of verdiepen.

## Begin hier

1. [README in de repo](../README.md)
2. [Installatie en ingebruikname](installatie-en-ingebruikname.md)
3. [Dashboard installeren](dashboard/README.md)
4. [Dashboardoverzicht](dashboardoverzicht.md)
5. **[v0.32 Release Handleiding](v0.32-release-handleiding.md)** — complete naslag van alle nieuwe features, entiteiten en aanbevolen volgorde
6. **[Ontluchtingsprotocol — CM97 (v0.40)](ontluchtingsprotocol-cm97-v0.40.md)** — geautomatiseerd ontluchten na vullen of bijvullen van het systeem
7. **[Element Only Heating — CM6 (v0.40)](element-only-heating-cm6-v0.40.md)** — exclusieve mode waarbij alleen het 3 kW DHW-tank-element verwarmt (HP off)
8. **[CWT-TM-8PT PT1000 module (v0.40)](cwt-tm-8pt-pt1000-module-v0.40.md)** — 8-kanaals PT1000 module op Modbus 3, vervangt de externe Dallas-bridge

Gebruik je de standaardfirmware, dan is de web installer uit de README meestal de snelste route. Daarna volg je de documentatieroute voor dashboard, uitleg en diagnose.

## Hoofdroute

- [Installatie en ingebruikname](installatie-en-ingebruikname.md): eerste installatie en controle na de eerste start.
- [Hardwarebeschrijving Duo + DHW (LilyGO)](hardware-dhw-lilygo.md): complete hardwarelijst, GPIO-mapping en bekabeling.
- [Hoe OpenQuatt werkt](hoe-openquatt-werkt.md): rolverdeling tussen thermostaat, OpenQuatt, warmtepomp en Home Assistant.
- [Heating Strategy](heating-strategy.md): overzicht van `Power House`, `Water Temperature Control` en `Adaptive Heating`, en wanneer welke aanpak past.
- [Power House](power-house.md): uitleg van de Power House-strategie, belangrijke parameters en Single/Duo-gedrag.
- [Water Temperature Control](water-temperature-control.md): uitleg van stooklijn, PID, curvegedrag en Duo-opbouw in watercurve-modus.
- [Adaptive Heating](adaptive-heating.md): zelflerende stooklijn die de laagst mogelijke aanvoertemperatuur opzoekt — beter rendement en minder compressorbelasting.
- [Dashboard installeren](dashboard/README.md): keuze van het juiste dashboardbestand en import in Home Assistant.
- [Dashboardoverzicht](dashboardoverzicht.md): de belangrijkste dashboardtabs en de volgorde waarin je ze gebruikt.
- [Diagnose en afstelling](diagnose-en-afstelling.md): diagnose, werkvolgorde en terughoudend afstellen.
- [Ontluchtingsprotocol — CM97 (v0.40)](ontluchtingsprotocol-cm97-v0.40.md): handleiding voor het geautomatiseerd ontluchten na vullen of onderhoud.
- [Element Only Heating — CM6 (v0.40)](element-only-heating-cm6-v0.40.md): handleiding voor exclusieve element-mode (HP uit, alleen 3 kW DHW-tank-element verwarmt).

Cooling en DHW worden beschreven in:

- [Hoe OpenQuatt werkt](hoe-openquatt-werkt.md): wat koeling wel en niet van OpenQuatt vraagt.
- [Dashboardoverzicht](dashboardoverzicht.md): waar je de cooling-tab en sensorconfiguratie gebruikt.
- [Koeling-instellingen](koeling-instellingen.md): alle instelbare koelparameters — emitterprofiel, dauwpuntbeveiliging, nacht-free cooling, minimale runtime en dual-HP.
- [DHW-instellingen](dhw-instellingen.md): alle instelbare DHW-parameters — temperaturen, tijdvenster, anti-kortcyclen, solar boost en legionellapreventie.

## Naslag

- [Heating Strategy](heating-strategy.md): bovenliggende uitleg van beide verwarmingsstrategieën.
- [Heating Strategy Development](heating-strategy-development.md): developer-uitleg van de strategy-interface, uitbreidregels, YAML-template en de grens tussen YAML-structuur en pure `.h` helpers.
- [Power House](power-house.md): aparte uitleg van huismodel, comfortlogica, rise/fall time, laaglastgedrag en Duo-keuze.
- [Power House thermisch model v0.32](power-house-thermisch-model-v0.32.md): UA-leerroutine, zonnewinst-correctie, voorspellend voorconditioneren, frost-zone derating en effectieve aanvoertemperatuur voor de perf_map.
- [Power House smart features v0.32](power-house-smart-features-v0.32.md): dynamisch tarief, PV-zelfconsumptie, window-open detectie, adaptieve Kp en SCOP/health/heat-up tracking.
- [DHW smart features v0.32](dhw-smart-features-v0.32.md): tariff-aware DHW, PV-zelfconsumptie DHW, adaptive usage pattern learning, tank standby-loss tracker, smart legionella deferral en time-to-ready.
- [Pressure protection v0.32](pressure-protection-v0.32.md): R32 drukbeveiliging per HP — soft/hard caps, HPS-mirror, peak EMA en fouling-trend monitoring.
- [Supply temp protection v0.32](supply-temp-protection-v0.32.md): per-HP watertemperatuur cap — soft/hard caps op uittredetemperatuur, hysterese, complementair aan systeem-brede `max_water_temp_limit_c`.
- [Persgasbeveiliging v0.52](discharge-protection-v0.52.md): per-HP begrenzing op persgastemperatuur — de vier trappen (90/98/105/110 °C), de zachte ladder en het samenspel met druk- en aanvoerbeveiliging.
- [Water Temperature Control](water-temperature-control.md): aparte uitleg van stooklijn, PID, curve phase/operating regime en Duo-hysterese.
- [Koeling-instellingen](koeling-instellingen.md): PI-regelaar, dauwpuntbeveiliging, nacht-free cooling, dual-HP en alle koelparameters.
- [DHW-instellingen](dhw-instellingen.md): toestandsmachine, temperatuurdrempels, tijdvenster, anti-kortcyclen, solar boost en legionellapreventie.
- [Regelgedrag van OpenQuatt](regelgedrag-van-openquatt.md): systeemstanden, overgangen en flowregeling.
- [Instellingen en meetwaarden](instellingen-en-meetwaarden.md): compile-time en runtime instellingen, plus de belangrijkste meetwaarden.
- [Ontwikkelen op Mac en WSL](ontwikkelen-op-mac-en-wsl.md): aanbevolen lokale ontwikkelworkflow voor onderhoud en builds.
- [OpenQuatt CiC Bridge](cic-bridge/README.md): aanvullende bridge-docs en een ESPHome-config om CiC-Modbusverkeer uit bestaande OpenQuatt-entities te emuleren.
