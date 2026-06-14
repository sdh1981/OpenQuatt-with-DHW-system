# DHW-instellingen

Deze pagina beschrijft alle instelbare parameters van de DHW-regeling in OpenQuatt. De regeling stuurt een 3-wegklep, een warmtepomp en een elektrisch boostelement aan via een eindige toestandsmachine (FSM).

## Toestandsmachine op hoofdlijnen

De DHW-controller doorloopt altijd dezelfde reeks toestanden:

```
IDLE_CV
  │
  ├─ legionellarun verschuldigd of geforceerd  ──► DHW_PREPARE ──► LEGIONELLA ──► IDLE_CV
  ├─ tank top < startdrempel                   ──► DHW_PREPARE ──► DHW_HEAT_PUMP
  │                                                                    │
  │                                               boost ingeschakeld ──► DHW_BOOST ──► IDLE_CV
  │                                               boost uitgeschakeld ─────────────► IDLE_CV
  └─ solar boost aangevraagd                  ──► DHW_PREPARE ──► DHW_BOOST ──► IDLE_CV

Foutafhandeling: elke toestand ──► FAULT (alleen te verlaten via "Reset DHW fout")
```

Elke overgang naar IDLE_CV registreert een cyclus-eindtijd die wordt gebruikt voor de minimale rusttijd.

## Temperatuurinstellingen

### DHW start top

- Entity: `number.openquatt_dhw_start_top`
- Standaard: **46 °C**
- Bereik: 38–50 °C (stap 0,5)

De bovenste tanksensor moet onder deze waarde zakken voordat een nieuwe warmtepompcyclus start. Een hogere waarde betekent minder vaak opstarten maar een koudere tank bij gebruik. Een lagere waarde levert vaker warm water maar meer cycli.

Pas dit aan als:
- de tank te snel afkoelt voor normaal gebruik → verlaag de drempel;
- de warmtepomp te vaak opstart → verhoog de drempel.

### DHW HP stop top

- Entity: `number.openquatt_dhw_hp_stop_top`
- Standaard: **49 °C**
- Bereik: 42–56 °C (stap 0,5)

Zodra de bovenste tanksensor deze waarde bereikt, stopt de warmtepompfase. Als "Element na HP-fase" aan staat, start daarna de boostelement-fase. Stel dit altijd minimaal 1–2 °C boven de startdrempel in om korte cycli te voorkomen.

### DHW HP aanvoer doel

- Entity: `number.openquatt_dhw_hp_target_flow`
- Standaard: **55 °C**
- Bereik: 48–60 °C (stap 0,5)

De aanvoertemperatuur die de warmtepomp nastreeft tijdens de DHW_HEAT_PUMP- en LEGIONELLA-fase. Een hogere aanvoer laadt de tank sneller maar verlaagt de COP. Pas dit aan op de specificaties van je boiler en spiraal.

### DHW boost doel

- Entity: `number.openquatt_dhw_boost_target`
- Standaard: **56 °C**
- Bereik: 55–58 °C (stap 0,5)

Het temperatuurdoel voor het elektrisch boostelement. Het element schakelt uit zodra de bovenste tanksensor deze waarde bereikt. Relevant als "Element na HP-fase" aan staat of bij een solar-boost aanvraag.

### DHW legionella doel

- Entity: `number.openquatt_dhw_legionella_target`
- Standaard: **61 °C**
- Bereik: 60–62 °C (stap 0,5)

De minimale temperatuur die 15 minuten aangehouden moet worden om de legionellarun als geslaagd te beschouwen. Verlaag dit niet onder 60 °C (wettelijke eis voor legionellapreventie).

## Bedrijfsmode-instellingen

### Element na HP-fase

- Entity: `switch.openquatt_dhw_boost_after_hp`
- Standaard: **aan**

Bepaalt of het elektrisch boostelement automatisch inschakelt nadat de warmtepompfase klaar is en de tank nog niet op boosttemperatuur zit. Schakel dit uit als je uitsluitend warmtepompcapaciteit wilt gebruiken (hogere COP, mogelijk minder warm water bij hoge vraag).

### DHW HP niveau

- Entity: `number.openquatt_dhw_hp_level`
- Standaard: **4**
- Bereik: 1–10

Het compressorniveau waarmee de warmtepomp draait tijdens de DHW-fase. Hogere niveaus laden de tank sneller maar verbruiken meer stroom en leveren mogelijk een lagere COP bij hoge aanvoertemperatuur.

## Anti-kortcyclen

### DHW minimum rust

- Entity: `number.openquatt_dhw_minimum_rest`
- Standaard: **1200 s (20 minuten)**
- Bereik: 0–3600 s (stap 60)

Minimale stilstandstijd tussen twee gewone DHW-cycli. Na het afronden van een cyclus (warmtepomp- of boostelement-fase) wacht de regeling minimaal deze tijd voordat een nieuwe cyclus gestart wordt, ook als de tank al onder de startdrempel zit.

Stel in op 0 om te deactiveren. Een waarde van 1200–1800 s is geschikt voor de meeste boilers. Legionella- en solar-boost-starts worden niet geblokkeerd door deze instelling.

## Tijdvenster

Met het tijdvenster beperk je DHW-starts tot een vast uurblok. Een lopende cyclus wordt nooit onderbroken als het venster sluit. Legionellaruns worden niet geblokkeerd.

### DHW tijdvenster inschakelen

- Entity: `switch.openquatt_dhw_window_enable`
- Standaard: **uit**

Schakel dit in om DHW-starts te beperken tot het geconfigureerde uurblok.

### DHW tijdvenster start uur

- Entity: `number.openquatt_dhw_window_start_hour`
- Standaard: **0** (middernacht)
- Bereik: 0–23 (stap 1)

Het eerste uur van het toegestane venster. Een waarde van 0 met einduur 7 betekent: DHW mag alleen starten tussen 00:00 en 07:00.

### DHW tijdvenster eind uur

- Entity: `number.openquatt_dhw_window_end_hour`
- Standaard: **7**
- Bereik: 0–23 (stap 1)

Het eerste uur buiten het toegestane venster. Een venster van 22:00 tot 06:00 (nachtdal) configureer je door start uur op 22 en eind uur op 6 in te stellen. De regeling herkent automatisch dat dit een nachtvenster is dat over middernacht loopt.

Voorbeelden:

| Start uur | Eind uur | Toegestaan |
|---|---|---|
| 0 | 7 | 00:00–07:00 (daluren) |
| 22 | 6 | 22:00–06:00 (nachtdal, over middernacht) |
| 0 | 0 | nooit (venster van nul uur) |

## Solar boost

### DHW solar boost auto

- Entity: `switch.openquatt_dhw_solar_boost_auto`
- Standaard: **uit**

Schakel in om automatisch een solar-boost te starten op basis van het stroomtarief. De tank wordt dan opgeladen met het element zolang het tarief op of onder de drempel staat.

### DHW zon-boost tarief drempel

- Entity: `number.openquatt_dhw_solar_tariff_threshold`
- Standaard: **0,02 EUR/kWh**
- Bereik: −0,10–0,20 EUR/kWh (stap 0,01)

Het stroomtarief waaronder een automatische solar-boost wordt gestart. Stel dit in op de prijs waarbij stroom goedkoop genoeg is om het element te laten draaien. Voorbeelden:

- **0,00**: alleen bij exact nul of negatief tarief
- **0,02**: tot 2 cent/kWh (typisch dalmoment bij dynamisch tarief)
- **0,08**: bij elke prijs onder 8 cent/kWh

Vereist dat "DHW solar boost auto" aan staat en dat een stroomtarief-entiteit gekoppeld is aan `${ha_electricity_tariff_entity_id}`.

## Flowbewaking

### DHW flow min

- Entity: `number.openquatt_dhw_flow_min`
- Standaard: **750 l/h**
- Bereik: 300–1300 l/h

Minimale waterdoorstroming die verwacht wordt terwijl de warmtepomp draait. Daalt de flow gedurende 30 seconden onder deze waarde, dan geeft de regeling een FLOW_OUT_OF_RANGE-fout.

### DHW flow max

- Entity: `number.openquatt_dhw_flow_max`
- Standaard: **1300 l/h**
- Bereik: 600–1800 l/h

Maximale waterdoorstroming. Overschrijding gedurende 30 seconden geeft eveneens een FLOW_OUT_OF_RANGE-fout.

Stel min en max ruim genoeg in om pompvariatie op te vangen, maar smal genoeg om echte storingen te detecteren.

## Legionellapreventie

De legionellarun vindt automatisch elke 7 dagen plaats. De run bestaat uit twee fasen:

1. Warmtepompfase: tank opladen tot HP stop top.
2. Boostfase met element: tank verhogen tot het legionelladoel en dit 15 minuten vasthouden.

De laatste succesvolle run en de geplande volgende run zijn zichtbaar in het dashboard als diagnostische tekstvelden.

Na een herstart van de firmware wordt de legionellatimer gevoed vanuit de persistent opgeslagen datum van de laatste run. De regeling start dus niet onmiddellijk een nieuwe run na een reboot als de vorige run recent was.

Forceer een run handmatig via "DHW source legionella force" of via een HA-automatisering die het bijbehorende HA-broninput-entiteit aanstuurt.

## Fouttoestanden

| Foutcode | Betekenis | Oplossing |
|---|---|---|
| SENSOR_IMPLAUSIBLE | Tanksensor buiten bereik of onplausibel | Controleer sensorbekabeling en broninstelling |
| HP_FAULT | Warmtepomp meldt een fout | Zie warmtepompdiagnose; reset via "Reset DHW fout" |
| VALVE_STUCK_CV | Klep bereikt DHW-positie niet binnen 20 s | Controleer bekabeling relais en klepfeedback |
| VALVE_MISMATCH | Klep staat in CV-positie terwijl DHW actief is | Controleer klepfeedback en relaiswerking |
| FLOW_OUT_OF_RANGE | Waterstroom buiten instelbaar bereik | Controleer pompwerking en flow min/max instellingen |
| TIMEOUT | Fase overschrijdt maximale runtime | Controleer tank, spiraal en warmtepompcapaciteit |
| LOCKOUT | Lockout actief tijdens lopende cyclus | Schakel lockout uit via HA of DHW-bronentiteit |

Alle fouten zijn latching: de regeling blijft in FAULT totdat je op "Reset DHW fout" drukt.

## Handmatige testmodus

Via "DHW manual test mode" overneem je directe controle over het kleprelais en het elementcontactor. De warmtepomp wordt dan niet aangestuurd door de DHW-regeling.

Gebruik dit uitsluitend voor bekabelingscontrole en inbedrijfstelling. Schakel handmatige modus altijd weer uit na gebruik.

## Diagnostiek

| Entiteit | Inhoud |
|---|---|
| `sensor.openquatt_dhw_state` | Huidige toestand als tekst |
| `sensor.openquatt_dhw_fault` | Huidige fout als tekst |
| `sensor.openquatt_dhw_state_code` | Toestandscode (0–5) |
| `sensor.openquatt_dhw_fault_code` | Foutcode (0–7) |
| `sensor.openquatt_dhw_target_flow_temp` | Aanvoerdoel dat naar de warmtepomp gestuurd wordt |
| `binary_sensor.openquatt_dhw_hp_request_active` | Of de warmtepomp op dit moment voor DHW draait |
| `binary_sensor.openquatt_dhw_block_cv_priority` | Of CV-vraag onderdrukt wordt |
| `sensor.openquatt_dhw_legionella_laatste_run` | Tijdstip van de laatste geslaagde legionellarun |
| `sensor.openquatt_dhw_legionella_volgende_run` | Verwacht tijdstip van de volgende run |
