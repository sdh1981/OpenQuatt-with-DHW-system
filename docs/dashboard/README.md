# Dashboard installeren

In deze map staan de dashboardbestanden voor OpenQuatt in Home Assistant.

## Welk bestand kies je?

Voor deze fork zijn er twee onderhouden dashboards, allebei Nederlands en Duo:

| Bestand | Wat het is |
|---|---|
| `openquatt_ha_dashboard_duo_nl.yaml` | **V1** — de gegroeide indeling, 11 tabs. Met de hand onderhouden. |
| `openquatt_ha_dashboard_duo_nl_v2.yaml` | **V2** — heringedeeld naar taak, 7 tabs. Gegenereerd. |

### V1

Elf tabs die de firmware-modules volgen: Overzicht, DHW, Energie, Flow, Warmteregeling, Koeling, Warmtepompen, Sensorconfiguratie, Instellingen, Service en test, Diagnostiek. Compleet, maar de pagina's Instellingen en Diagnostiek zijn met ruim honderd entiteiten eerder een lijst dan een scherm.

### V2

Zeven tabs, ingedeeld naar wat je aan het doen bent in plaats van naar waar de code staat:

| Tab | Vraag die hij beantwoordt |
|---|---|
| Nu | Is alles goed? |
| Warm water | Wat doet de boiler, en waar draai ik aan? |
| Verwarmen | Welke strategie draait, en wat vragen de HP's? |
| Koelen | Mag er gekoeld worden, en waarom wel of niet? |
| Energie | Wat heeft het gekost en opgeleverd? |
| Afstellen | Alle instelbare waarden, gegroepeerd naar beslissing. |
| Uitzoeken | Waarom doet het systeem wat het doet? |

De sectie **Aandacht** op de eerste tab werkt met voorwaardelijke kaarten: die verschijnen alleen als er iets is. Een lege sectie betekent dat alles in orde is — geen scherm vol groene vinkjes.

V2 wordt gegenereerd:

```bash
python scripts/build_dashboard_v2.py
```

Dat script leest eerst uit de firmware-YAML welke entiteiten er werkelijk worden aangemaakt en valideert daarna elke verwijzing daartegen. Een tikfout in een entity-id wordt zo een harde fout in plaats van een leeg vakje in Home Assistant. Bewerk dus het script, niet het YAML-bestand.

### Uit het bovenliggende project

De volgende bestanden komen van [OpenQuatt](https://github.com/OpenQuatt/OpenQuatt) en zijn hier **niet** onderhouden. Ze lopen achter en verwijzen naar entiteiten die in deze fork niet meer bestaan:

- `openquatt_ha_dashboard_duo_en.yaml`
- `openquatt_ha_dashboard_single_nl.yaml`
- `openquatt_ha_dashboard_single_en.yaml`

## Importeren in Home Assistant

1. Open Home Assistant.
2. Ga naar **Instellingen -> Dashboards**.
3. Maak een nieuw dashboard aan of open een bestaand dashboard.
4. Open het menu met de drie puntjes.
5. Kies **Raw configuration editor**.
6. Plak de inhoud van het gekozen YAML-bestand.
7. Sla op en laad het dashboard opnieuw.

## Bij importproblemen

- Controleer of je echt het juiste `single`- of `duo`-bestand hebt.
- Controleer of je de volledige YAML hebt geplakt.
- Controleer of de OpenQuatt-entiteiten al in Home Assistant bestaan.

## Optioneel: dynamische bronselectie via Home Assistant

Gebruik `openquatt_ha_dynamic_sources_package.yaml` alleen als je tijdens runtime zelf Home Assistant-bronnen wilt kunnen aanwijzen zonder opnieuw te flashen.

Dat pakket maakt extra helper-entiteiten aan, zoals:

- `input_text.openquatt_source_outdoor_temperature`
- `input_text.openquatt_source_room_setpoint`
- `input_text.openquatt_source_room_temperature`

Installatie in Home Assistant:

1. Zet packages aan in `/config/configuration.yaml`.
2. Kopieer het pakket naar `/config/packages/openquatt_dynamic_sources.yaml`.
3. Herlaad de template-entiteiten of herstart Home Assistant.

## Belangrijk om te onthouden

- De dashboards gaan uit van de entiteitsnamen uit deze repository.
- Als je zelf entiteitsnamen wijzigt, moet je ook het dashboard aanpassen.
- Het Nederlandstalige dashboard is voor de meeste gebruikers de beste start.
