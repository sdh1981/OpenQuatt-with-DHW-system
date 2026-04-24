# Hardwarebeschrijving - OpenQuatt Duo + DHW (LilyGO T-Connect)

Deze pagina beschrijft de gebruikte hardware en pinmapping van de huidige `Duo + DHW` opstelling.

## Systeemoverzicht

- Warmtepomp: Quatt Duo (2 ODU's)
- Controller: LilyGO T-Connect (ESP32-S3)
- Boiler: Inventum horizontaal 220L
- 3-wegklep: Honeywell V4044 (veerretour)
- Boostverwarming: 3 kW element via contactor
- Temperatuursensoren:
  - Tank top/bottom via externe DHW-sensornode op Modbus RTU (RS485 #3)
  - Spiraal in/out via 2x MAX31865 + 2-draads PT1000

## Actuatoren

- Relais 1 (3-wegklep DHW/CV):
  - GPIO: `GPIO40`
  - Entity: `switch.openquatt_dhw_valve_relay`
- Relais 2 (boiler boost/contactorspoel):
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
- RS485 #2 (CIC):  
  - TX `GPIO6`, RX `GPIO7`
- RS485 #3 (DHW Modbus sensornode):  
  - TX `GPIO17`, RX `GPIO18`
  - 19200 baud, 8E1, slave-id 1

## MAX31865 (dual, PT1000)

Gedeelde SPI:

- SCK: `GPIO41`
- MOSI: `GPIO39`
- MISO: `GPIO38`

Chip-selects:

- MAX31865 #1 CS: `GPIO47` (coil in)
- MAX31865 #2 CS: `GPIO2` (coil out)

Sensorinstellingen:

- RTD type: PT1000
- `rtd_nominal_resistance`: 1000 ohm
- `reference_resistance`: 4300 ohm
- `rtd_wires`: 2

Belangrijk:
- Eerdere mapping met `GPIO35`/`GPIO37` is verlaten vanwege PSRAM-conflict op ESP32-S3R8.

## DS18B20

- 1-Wire pin: `GPIO42`
- Gebruikt voor lokale temperatuurmeting indien aangesloten.

## Voeding en bekabeling (praktisch)

- LilyGO T-Connect: via USB-C / stabiele 5V voeding.
- Relaisbord (2-kanaals):
  - VCC/JD-VCC op 5V (volgens module-specificatie)
  - GND gemeenschappelijk met LilyGO GND
  - IN1/IN2 naar GPIO40/GPIO48
- MAX31865:
  - Voed met 3.3V (logicaniveau ESP32-S3)
  - GND gemeenschappelijk met LilyGO
  - SPI-lijnen kort houden en degelijk afschermen bij langere trajecten
- PT1000 op leiding:
  - Goede thermische koppeling + isolatie van meetpunt
  - Voorkom trekbelasting op sensorkabel

## Gebruikte configuratiebestanden

- `openquatt_duo_lilygo_tconnect+cic.yaml`
- `openquatt/profiles/oq_substitutions_lilygo_tconnect_cic.yaml`
- `openquatt/oq_boiler_control.yaml`
- `openquatt/oq_local_sensors.yaml`
- `openquatt/oq_dhw_modbus_bridge.yaml`

## Hoe DHW werkt

De DHW-regeling draait als finite state machine (FSM) met veilige interlocks.

### States

- `IDLE_CV`: klep in CV-stand, element uit, normale ruimteverwarming.
- `DHW_PREPARE`: klep naar boilerpad schakelen en feedback controleren.
- `DHW_HEAT_PUMP`: DHW laden met warmtepomp.
- `DHW_BOOST`: naverwarmen met elektrisch element (bijv. boost/solar).
- `LEGIONELLA`: periodieke cyclus naar hogere temperatuur.
- `FAULT`: veilige toestand bij fout.

### Normale DHW-cyclus

1. Startvoorwaarde: `tank_top < startdrempel` (typisch 46 C), geen lockout/fout.
2. Klep schakelt naar DHW-pad (boiler), daarna pas warmtepompaanvraag.
3. Warmtepomp laadt tot stopdrempel (`tank_top >= 49 C`) of timeout.
4. Indien nodig volgt `DHW_BOOST` met element naar hogere doelwaarde (55-58 C).
5. Daarna terug naar `IDLE_CV`.

### Legionella-cyclus

- Periodiek (typisch wekelijks).
- Eerst maximaal bruikbaar opwarmen met warmtepomp.
- Daarna element bijschakelen naar 60-62 C.
- Run wordt gelogd (laatste/volgende run in diagnostiek).

### Veiligheid en interlocks

- Element alleen toegestaan als DHW-pad bevestigd is.
- Bij reboot: veilige defaults (klep naar rust/CV, element uit).
- Sensor-plausibiliteitschecks en flow-grenzen.
- Valve mismatch/feedbackfouten leiden naar `FAULT`.
- In `FAULT`: DHW-aanvraag uit, element uit, foutcode gepubliceerd.

## Veiligheidsnotities

- Element wordt alleen aangestuurd in DHW-pad met interlocks in de FSM.
- Bij fouttoestand valt regeling terug naar veilige toestand (CV-pad, element uit).
- Gebruik altijd een geschikte zekering, contactor en bekabelingsdoorsnede voor 3 kW belasting.
