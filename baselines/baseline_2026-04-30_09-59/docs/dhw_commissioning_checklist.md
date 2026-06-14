# DHW Commissioning Checklist

Deze checklist is bedoeld om de DHW-regeling eerst stabiel en reproduceerbaar werkend te krijgen, voordat optimalisatie en AI-functies worden toegevoegd.

## 1. Hardware sanity

- [ ] Relais-uitgangen gecontroleerd:
  - [ ] 3-wegklep relais op `GPIO40`
  - [ ] Element/contactor relais op `GPIO48`
- [ ] Klepfeedback gecontroleerd (DHW/CV logica klopt)
- [ ] PT1000 sensoren geven livewaarden (coil in/out)

## 2. Sensorvalidatie

- [ ] Tank `top` en `bottom` correct gemapt (niet omgedraaid)
- [ ] Coil in/out plausibel en stabiel
- [ ] Flowwaarde plausibel tijdens HP-run
- [ ] HP fault en lockout input getest

## 3. DHW FSM test

- [ ] `IDLE_CV -> DHW_PREPARE -> DHW_HEAT_PUMP` werkt
- [ ] Stopconditie op `hp_stop_top` werkt
- [ ] `DHW_BOOST` start/stop op doeltemperatuur werkt
- [ ] `LEGIONELLA` force-run werkt

## 4. Interlocks en safety

- [ ] Element nooit actief zonder bevestigde DHW-klepstand
- [ ] Valve mismatch fault triggert correct
- [ ] Timeout faults triggeren correct
- [ ] Reboot-safe gedrag klopt:
  - [ ] klep naar CV
  - [ ] element uit

## 5. Fault handling

- [ ] `DHW clear fault` werkt betrouwbaar
- [ ] Fault blijft weg als oorzaak weg is
- [ ] Fault komt terug als oorzaak blijft (verwacht gedrag)

## 6. Dashboard/UX

- [ ] Alle gebruikte DHW-entiteiten bestaan
- [ ] Waarden en statussen kloppen visueel
- [ ] Overbodige debug-secties verborgen/verplaatst
- [ ] Solar boost auto en tariefstatus duidelijk zichtbaar

## 7. Kalibratie en tuning

- [ ] Vaste PT1000 correcties bevestigd:
  - [ ] coil in offset
  - [ ] coil out offset
- [ ] DHW flow min/max op installatie afgesteld
- [ ] Boost target en legionella target bevestigd

## 8. Duurtest

- [ ] 24-48 uur proefrun zonder handmatige ingreep
- [ ] Geen onverwachte fault-latches
- [ ] State transitions en logs gecontroleerd

## 9. Release baseline

- [ ] Werkende versie gecommit/tagged als baseline
- [ ] Korte changelog toegevoegd
- [ ] Testresultaten opgeslagen

