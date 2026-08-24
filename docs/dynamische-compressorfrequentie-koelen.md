# Dynamische compressorfrequentie bij koelen

Werkdocument. Het idee: de compressorstand vastzetten op 1 en in plaats daarvan bijstellen wat stand 1 in Hz betekent, zodat je continu moduleert in plaats van in tien sprongen.

Dit is een **experiment in fasen met een go/no-go halverwege**, geen regelstrategie die je aanzet. Fase 3 beslist of fase 4 überhaupt gebouwd wordt.

## Waarom koelen en niet verwarmen

Drie dingen, alle drie nagekeken in de code:

**De koelstrategie gebruikt `hp_perf_map.h` niet.** Nul verwijzingen in `oq_cooling_strategy.yaml`. Bij verwarmen is dat het zwaarste bezwaar — zes modules voorspellen daar vermogen en COP uit een tabel die op standen is geijkt, en die gaat liegen zodra een stand een andere frequentie betekent. Koelen regelt op aanvoertemperatuurfout en heeft dat probleem niet.

**De condensbeveiliging werkt op een gemeten waarde.** `oq_cooling_safety.yaml` berekent een dauwpunt per ruimte en voedt `cooling_effective_min_supply_temp`; de strategie neemt daarvan het maximum met `cooling_min_supply_temp`. Dat is een vloer op de gemeten aanvoertemperatuur en die blijft werken wat je ook met frequenties doet.

**De faalwijze is zacht.** Gaat de buitenunit spanningsloos, dan staat de fabriekstabel terug en is stand 1 weer 30 Hz. Bij verwarmen betekent dat te weinig vermogen en wordt het koud. Bij koelen betekent het *méér* koeling dan bedoeld, en dan grijpen de aanvoervloer en de dauwpuntbewaking in.

**Je hebt het niveau al vastgezet.** `Cooling demand max` staat op 1, dus de vraag wordt op 1 geklemd en de stand is altijd 1.

## Wat je nodig hebt

| Entiteit | Waarvoor |
|---|---|
| `sensor.openquatt_hp1_compressor_frequency` | wat de compressor **werkelijk** draait (reg 2103) |
| `sensor.openquatt_hp1_compressor_frequency_demand` | de limiet die de unit **oplegt** (reg 2102) — zie de naamval hieronder |
| `binary_sensor.openquatt_hp1_protection_compressor_oil_return` | olieretour actief; staat `disabled_by_default`, dus aanzetten |
| `binary_sensor.openquatt_hp1_compressor_frequency_limited` | unit knijpt op druk of omgeving |
| `sensor.openquatt_hp1_evaporator_pressure` | zakt hij weg bij lage frequentie |
| `sensor.openquatt_hp1_cooling_power` / `_power_input` | koelvermogen en opgenomen vermogen |
| `sensor.openquatt_cooling_supply_error` | aanvoer minus doel; positief = te warm |
| `number.openquatt_hp1_experimenteel_koelen_f1_hz` | de knop waar het om draait |

> **Naamval.** `Compressor frequency demand` heet zo, maar leest register 2102 en dat is volgens het protocoldocument *"Compressor frequency limit at current time (smaller or equal to current setting value)"*. Het is dus geen vraag maar een plafond. Schrijf je 45 Hz en zie je hier 40, dan begrenst de unit je.

## Fase 0 — nulmeting, niets schrijven

Zonder ijkpunt kun je later niet zeggen of het beter werd.

1. Zet de olieretour-sensor aan voor beide units
2. Zet de tweede compressor uit: `number.openquatt_cooling_hp2_assist_level` op **0** **én** `switch.openquatt_cooling_auto_assist_2nd_compressor` **uit** — allebei nodig, want 0 op die schuif betekent "automaat beslist", niet "uit"
3. Een etmaal laten draaien

Noteren:

| | |
|---|---|
| starts per 2 uur | |
| looptijd per start | |
| aanvoertemperatuur, bereik | |
| `cooling_supply_error`, bereik | |
| olieretour gezien? zo ja, hoe vaak | |

## Fase 1 — meten wat een frequentie werkelijk doet

Unit draait, `schrijven tijdens bedrijf` aan, stappen omhoog. Maximaal 5 Hz per keer, dus 30 → 35 → 40 → 45.

Per stap laten stabiliseren en noteren:

| Geschreven | Werkelijk (2103) | Limiet (2102) | Koelvermogen | Opgenomen | Aanvoer | COP |
|---|---|---|---|---|---|---|
| 30 | | | | | | |
| 35 | | | | | | |
| 40 | | | | | | |
| 45 | | | | | | |

**De belangrijkste kolom is "werkelijk".** Volgt de unit wat je schrijft? Register 3282 staat op *frequency limitation by outdoor unit*, dus hij mag zelf knijpen. Loopt het uiteen, dan meet je iets anders dan je denkt en heeft doorgaan geen zin voordat je weet waarom.

## Fase 2 — omlaag, tot iets klaagt

30 → 25 → 20 → … in stappen van 5.

Afbreken zodra één van deze vier begint:

| | |
|---|---|
| olieretour komt **vaker dan om de 2 uur** | |
| werkelijke frequentie volgt het geschrevene niet meer | |
| `compressor_frequency_limited` gaat aan | |
| verdamperdruk zakt weg | |

Noteren per stap:

| Geschreven | Werkelijk | Koelvermogen | Olieretour-interval | Bijzonderheden |
|---|---|---|---|---|
| 25 | | | | |
| 20 | | | | |
| 15 | | | | |

**Dit getal beslist alles.** Hoe laag kun je werkelijk houdbaar draaien.

## Fase 3 — go / no-go

Leg het koelvermogen bij je laagste houdbare frequentie naast de werkelijke koelvraag van je huis.

- **Minimum lager dan de vraag** → continu draaien wordt mogelijk, de winst is echt, ga door naar fase 4
- **Minimum nog steeds hoger** → dynamische frequentie lost je pendelen niet op. **Stoppen.**

Reken niet mee op kleine stappen. Een verdringercompressor levert ongeveer evenredig met toerental:

| Frequentie | Ongeveer | Betekenis |
|---|---|---|
| 28 Hz | 93 % | verandert niets aan pendelen |
| 20 Hz | 67 % | begint te tellen |
| 15 Hz | 50 % | verschuift het hele lastprofiel |

## Fase 4 — automatiseren, alleen bij groen

Nieuwe code. Schets:

- trage lus, één stap van 5 Hz per keer, aangestuurd door `cooling_supply_error`
- begrensd tussen de in fase 2 gemeten vloer en 30 Hz
- de bestaande aanvoervloer en dauwpuntbewaking blijven er onaangeroerd bovenop

**Harde eis vooraf: een terugleescontrole.** De tabel is vluchtig. Gaat de buitenunit spanningsloos, dan staat stand 1 weer op 30 Hz terwijl de regeling denkt 22 te draaien. Zonder die controle merkt niemand dat.

## Wat dit niet is

Geen permanente wijziging. `hp_perf_map.h` blijft geldig zolang je de tabel na de proef terugzet, en dat gaat vanzelf zodra de buitenunit een keer spanningsloos is geweest.

En dit staat los van schrijven naar de EEPROM-chip zelf — dat is nog steeds niet gebouwd. Zie [odu-eeprom-parameters.md](odu-eeprom-parameters.md).
