# Hardwarebeschrijving - OpenQuatt Duo + DHW (LilyGO T-Connect)

Deze pagina beschrijft de gebruikte hardware en pinmapping van de huidige
`Duo + DHW` opstelling (v0.40+). Sinds v0.40 worden de tank- en spiraalsensoren
gelezen via de **CWT-TM-8PT** PT1000-module op Modbus RTU bus 3; de eerdere
externe DS18B20-sensornode en de lokale MAX31865-chips zijn vervallen.

## Systeemoverzicht

- Warmtepomp: Quatt Duo (2 ODU's)
- Controller: LilyGO T-Connect (ESP32-S3)
- Boiler: Inventum horizontaal 220L
- 3-wegklep: Honeywell V4044 (veerretour)
- Boostverwarming: 3 kW element via contactor
- Temperatuursensoren: **CWT-TM-8PT** 8-kanaals PT1000-module op RS485 #3
  (zie [CWT-TM-8PT module](cwt-tm-8pt-pt1000-module-v0.40.md))

## Fasebedrading (belangrijk voor vermogen)

- **HP1 + HP2 (beide compressoren): fase L1**
- **3 kW DHW-element: fase L2**

De warmtepompen en het boilerelement staan op **gescheiden fasen**. Daardoor is
de gecombineerde stroom (HP + element gelijktijdig, bijv. tijdens boost-met-
HP-assist) géén overbelasting van één groep. De interne 16 A-supervisory bewaakt
alleen het HP-vermogen op L1; het element op L2 loopt daar onafhankelijk van.

## Actuatoren

- Relais 1 (3-wegklep DHW/CV):
  - GPIO: `GPIO40`
  - Entity: `switch.openquatt_dhw_valve_relay`
- Relais 2 (boiler boost / contactorspoel 3 kW element):
  - GPIO: `GPIO48`
  - Entity: `switch.openquatt_dhw_element_contactor`

Opmerking:
- In firmware zijn beide relais als active-low geconfigureerd (`inverted: true`).

## Klepfeedback (aux-contact)

2-draads dry-contact met drive + sense:

- Drive GPIO: `GPIO11` (wordt hoog gehouden)
- Sense GPIO: `GPIO13` (input)

Logica:
- Aux-contact gesloten = DHW-positie bevestigd.
- CV-positie wordt intern als inverse daarvan afgeleid.

## RS485-bussen

- RS485 #1 (OpenQuatt naar warmtepomp):
  - TX `GPIO4`, RX `GPIO5`, RTS/DE `GPIO21`
  - 19200 baud, 8E1, slave-id's 0x01 (HP1) en 0x02 (HP2)
- RS485 #2 (CIC):
  - TX `GPIO6`, RX `GPIO7`
  - Draait als **Modbus-slave** richting de Quatt CIC (`oq_cic_modbus_slave.yaml`),
    19200 8E1, slave-id 3. Geen forwarding naar de HP-bus.
  - Status-LEDs (APA102 op de T-Connect) via SPI: CLK `GPIO3`, MOSI `GPIO8`.
- RS485 #3 (CWT-TM-8PT PT1000-module):
  - TX `GPIO17`, RX `GPIO18`, RTS `GPIO39` (auto-direction transceiver → ongebruikt)
  - **9600 baud, 8N1, slave-id 1**

## CWT-TM-8PT (PT1000-sensoren, Modbus 3)

8-kanaals PT1000-module (ComWinTop). Kanaaltoewijzing in deze build:

| Kanaal | Functie | Modbus-register (holding) |
|---|---|---|
| Ch1 | DHW tank top | `0x68` |
| Ch2 | DHW tank bottom | `0x69` |
| Ch3 | DHW spiraal in | `0x6A` |
| Ch4 | DHW spiraal uit | `0x6B` |
| Ch5 | T-Aanvoer (CV) | `0x6C` |
| Ch6–Ch8 | Spare | `0x6D`–`0x6F` |

- Waarde = `S_WORD × 0.1 °C` (signed, ondersteunt negatieve temperaturen).
- Voeding 8–30 V DC; DIN-rail montage.
- Details + registermap: [CWT-TM-8PT module](cwt-tm-8pt-pt1000-module-v0.40.md).

### Vervallen sinds v0.40

- De **2× MAX31865** (PT1000 spiraal in/uit) zijn vervallen. De SPI-pinnen
  `GPIO38`, `GPIO41`, `GPIO47` en `GPIO2` zijn nu vrij (`GPIO39` is RS485 #3 RTS).
- De **externe DS18B20-sensornode** op Modbus is vervallen (vervangen door CWT).

## DS18B20 (optioneel, lokaal)

- 1-Wire pin: `GPIO42`
- Optionele lokale temperatuurmeting indien aangesloten; niet vereist voor de
  DHW-regeling (die gebruikt de CWT-kanalen).

## Voeding en bekabeling (praktisch)

- LilyGO T-Connect: via USB-C / stabiele 5V voeding.
- Relaisbord (2-kanaals):
  - VCC/JD-VCC op 5V (volgens module-specificatie)
  - GND gemeenschappelijk met LilyGO GND
  - IN1/IN2 naar `GPIO40`/`GPIO48`
- CWT-TM-8PT:
  - Voeding via 12/24 V rail (8–30 V DC)
  - RS485 A/B naar RS485 #3 (`GPIO17`/`GPIO18`)
  - GND gemeenschappelijk met LilyGO
- PT1000 op leiding:
  - Goede thermische koppeling + isolatie van meetpunt
  - Voorkom trekbelasting op sensorkabel
- 3 kW element: gebruik altijd een geschikte zekering, contactor en
  bekabelingsdoorsnede; element op fase L2 (zie Fasebedrading).

## Gebruikte configuratiebestanden

- `openquatt_duo_lilygo_tconnect+cic.yaml` (entrypoint)
- `openquatt/profiles/oq_substitutions_lilygo_tconnect_cic.yaml` (pinmapping)
- `openquatt/oq_boiler_control.yaml` (DHW-FSM, relais, klep)
- `openquatt/oq_cwt_pt_module.yaml` (PT1000-sensoren op Modbus 3)
- `openquatt/oq_cic_modbus_slave.yaml` (CIC-slave op RS485 #2)
- `openquatt/oq_local_sensors.yaml` (optionele lokale sensoren)

## Hoe DHW werkt

De DHW-regeling draait als finite state machine (FSM) met veilige interlocks.

### States

- `IDLE_CV`: klep in CV-stand, element uit, normale ruimteverwarming.
- `DHW_PREPARE`: klep naar boilerpad schakelen en feedback controleren.
- `DHW_HEAT_PUMP`: DHW laden met warmtepomp.
- `DHW_BOOST`: naverwarmen met elektrisch element (bijv. boost/solar).
- `LEGIONELLA`: periodieke cyclus naar hogere temperatuur.
- `FAULT`: veilige toestand bij fout.

Volledige beschrijving van de regeling en alle instellingen:
[DHW: werking en instellingen](dhw-instellingen.md). Hieronder de hoofdlijn.

### Normale DHW-cyclus

1. **Start** bij `tank_top <` startdrempel (standaard 46 °C), zonder lockout of fout.
2. **Klep** schakelt naar het DHW-pad (boiler); pas daarna gaat de warmtepompaanvraag uit.
3. **Warmtepomp** laadt tot de bodemsensor `tank_bottom >= 52 °C`.
   - Die grens staat vast in de firmware.
   - Valt terug op `tank_top >= 49 °C` als de bodemsensor ontbreekt.
   - Hooguit 180 min.
   - In Duo draaien beide units, of met single-HP mode één lead met eventueel een stapsgewijze tweede-HP assist.
4. **Natraject:** indien nodig volgt `DHW_BOOST` met het element naar de boost-doelwaarde (standaard **56 °C**).
   - Optioneel (standaard uit): **boost HP-assist**. Is de tankbodem bij de boost-start koud (< 35 °C), dan draaien element én warmtepomp samen. De HP stopt zodra `tank_top >= 52 °C`; het element gaat door tot de boost-doelwaarde.
5. Daarna terug naar `IDLE_CV`.

**Snelboost** (`DHW boost now`): beide warmtepompen en het element tegelijk.
- De HP's stoppen bij een tanktop van 55 °C.
- Het element gaat door tot 60 °C.

### Legionella-cyclus

- **Planning:** elke 7 dagen na de laatste geslaagde run.
- **Fase 1:** warmtepomp en element samen. De HP stopt bij een tankbodem van 53 °C, of bij een tanktop van 55 °C (plafond tegen hoge perszijdedruk).
- **Fase 2:** element alleen naar de legionella-doelwaarde **68 °C** (Inventum-vereiste), met een hold van 15 min.
- **Klep:** blijft de hele run op DHW, zodat de pomp de tank mengt via de spiraal.
- **Logging:** laatste en volgende run staan in de diagnostiek.

### Veiligheid en interlocks

- Element alleen toegestaan als DHW-pad bevestigd is.
- Bij reboot: veilige defaults (klep naar rust/CV, element uit).
- Sensor-plausibiliteitschecks en flow-grenzen.
- Valve mismatch/feedbackfouten leiden naar `FAULT`.
- In `FAULT`: DHW-aanvraag uit, element uit, foutcode gepubliceerd.

## Veiligheidsnotities

- Element wordt alleen aangestuurd in DHW-pad met interlocks in de FSM.
- Bij fouttoestand valt regeling terug naar veilige toestand (CV-pad, element uit).
- Gebruik altijd een geschikte zekering, contactor en bekabelingsdoorsnede voor
  3 kW belasting. Element op fase L2, gescheiden van de HP's op L1.
