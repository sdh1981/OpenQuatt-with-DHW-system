# OpenQuatt

<img src="docs/assets/openquatt_logo.svg" alt="OpenQuatt logo" width="400" />

OpenQuatt is ESPHome-firmware voor Quatt Single- en Duo-installaties. Het project geeft je meer inzicht, meer regie en een beter uitleesbaar Home Assistant-dashboard bovenop de bestaande Quatt-hardware.

> [!WARNING]
> Dit project zit nog in een experimentele fase. Gebruik het bewust en test wijzigingen stap voor stap.
>
> Gebruik van OpenQuatt kan gevolgen hebben voor je Quatt-garantie. De standaard commerciële Quatt-garantie vervalt in principe bij gebruik van externe aansturing zoals OpenQuatt. De wettelijke garantie blijft bestaan, maar een garantieclaim kan daardoor in de praktijk wel ingewikkelder worden.

## Wat is OpenQuatt?

OpenQuatt is bedoeld voor gebruikers van een Quatt Single of Quatt Duo die:

- meer inzicht willen in wat de installatie doet;
- een duidelijk Home Assistant-dashboard willen;
- meer grip willen op gedrag, metingen en instellingen;
- willen kunnen kiezen uit de twee ondersteunde OpenQuatt-hardwareprofielen.

Je hoeft voor de eerste installatie niet eerst alle technische achtergronddocumenten te lezen. De hoofdroute is: installeren, koppelen aan Home Assistant en daarna pas verdiepen waar nodig.

## Ondersteunde combinaties

OpenQuatt ondersteunt momenteel deze Quatt-opstellingen en hardwarevarianten.

Quatt-opstellingen (V1 en V1.5):

- Single
- Duo

Ondersteunde hardware:

- Waveshare ESP32-S3-Relay-1CH
- Heatpump Listener

Elke combinatie van bovenstaande Quatt-opstellingen en hardware wordt ondersteund:

- Single + Waveshare ESP32-S3-Relay-1CH
- Single + Heatpump Listener
- Duo + Waveshare ESP32-S3-Relay-1CH
- Duo + Heatpump Listener

## Snel starten

1. Open de [OpenQuatt installer](https://jeroen85.github.io/OpenQuatt/install/).
2. Kies exact de combinatie die past bij jouw installatie en hardware.
3. Flash de firmware via USB in Chrome of Edge.
4. Stel wifi in via de browserflow of via het fallback access point `OpenQuatt` met wachtwoord `openquatt`.
5. Voeg het apparaat toe in Home Assistant.
6. Importeer het dashboard dat past bij jouw taal en opstelling:
   - Duo NL: [docs/dashboard/openquatt_ha_dashboard_duo_nl.yaml](docs/dashboard/openquatt_ha_dashboard_duo_nl.yaml)
   - Duo EN: [docs/dashboard/openquatt_ha_dashboard_duo_en.yaml](docs/dashboard/openquatt_ha_dashboard_duo_en.yaml)
   - Single NL: [docs/dashboard/openquatt_ha_dashboard_single_nl.yaml](docs/dashboard/openquatt_ha_dashboard_single_nl.yaml)
   - Single EN: [docs/dashboard/openquatt_ha_dashboard_single_en.yaml](docs/dashboard/openquatt_ha_dashboard_single_en.yaml)

Alleen de nieuwste stabiele eerste-installatiebestanden worden standaard via de installer aangeboden.

Loopt de eerste installatie vast, kijk dan hier:

- [Dashboard installeren](docs/dashboard/README.md)
- [Diagnose en afstelling](docs/diagnose-en-afstelling.md)

## Ondersteunde hardware

OpenQuatt richt zich nu bewust op twee hardwareprofielen:

- [Waveshare ESP32-S3-Relay-1CH](https://www.waveshare.com/esp32-s3-relay-1ch.htm)
- [Heatpump Listener](https://electropaultje.nl/product/heatpump-listener/)

## Documentatie

Begin hier:

- [Documentatie-overzicht](docs/README.md)

Belangrijkste pagina's voor gebruikers:

- [Installatie en ingebruikname](docs/installatie-en-ingebruikname.md) voor installeren en controle na de eerste start
- [Hardwarebeschrijving Duo + DHW (LilyGO)](docs/hardware-dhw-lilygo.md) voor complete hardwarelijst, GPIO-mapping en bekabeling
- [Hoe OpenQuatt werkt](docs/hoe-openquatt-werkt.md) voor de rolverdeling tussen thermostaat, OpenQuatt, warmtepomp en Home Assistant
- [Dashboardoverzicht](docs/dashboardoverzicht.md) voor gebruik en diagnose van de dashboardweergaven
- [Dashboard installeren](docs/dashboard/README.md) voor het importeren van dashboards
- [Diagnose en afstelling](docs/diagnose-en-afstelling.md) voor diagnose en gerichte afstelling

Technischere naslag blijft beschikbaar, maar staat niet meer centraal in de publieksroute:

- [Regelgedrag van OpenQuatt](docs/regelgedrag-van-openquatt.md)
- [Instellingen en meetwaarden](docs/instellingen-en-meetwaarden.md)

## EnergyOS-integratie

De `+CIC`-build (LilyGO T-Connect) ondersteunt een directe koppeling met [Energy OS](https://github.com/sdh1981/energy-os) voor gecombineerde HP- en batterijsturing.

### Wat het doet

| Functie | Detail |
|---|---|
| **HP cap extern schrijven** | EOS kan de warmtepomp extern throttlen via `number.openquatt_eos_hp_cap_ha` (0–20). De cap werkt alleen naar beneden — OpenQuatt's eigen supervisory (16 A grid guard) heeft altijd prioriteit. |
| **Comfort floor** | EOS respecteert een buitentemperatuurafhankelijk minimum en verlaagt de cap nooit zover dat comfort in het gedrang komt. |
| **PV-voorrang** | Als zonnepanelen meer leveren dan de HP verbruikt, geeft EOS de HP vrij ongeacht de batterijmodus. |
| **DHW-vensters** | EOS schrijft de goedkoopste en duurste tariefvensters door naar HA-datetimes die OpenQuatt leest voor legionellabescherming en DHW-opwarming. |

### Activering

1. Het package `openquatt/oq_energy_os_bridge.yaml` is al opgenomen in `openquatt_duo_lilygo_tconnect+cic.yaml`.
2. Compileer en flash de firmware:
   ```
   python -m esphome compile openquatt_duo_lilygo_tconnect+cic.yaml
   python -m esphome upload openquatt_duo_lilygo_tconnect+cic.yaml
   ```
3. Verwijder na de flash het commentaar bij `number.set_value` in `script.eos_apply_hp_cap` (eos_03_dispatcher.yaml in Energy OS).

Na activering verschijnt `number.openquatt_eos_hp_cap_ha` als entiteit in Home Assistant.

### Veiligheidsgedrag

- Default waarde is 20 (geen beperking) — als Energy OS niet actief is doet de brug niets.
- OpenQuatt's eigen supervisory verlaagt de cap altijd verder indien het grid dat vereist.
- De brug kan de cap nooit verhogen, alleen verlagen.

## Licentie

Dit project bevat een `LICENSE`-bestand in de root van de repository.
