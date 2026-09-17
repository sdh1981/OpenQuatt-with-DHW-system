# Power House-logica uit upstream OpenQuatt v0.50.0

`openquatt/includes/v050/` bevat de beslislogica van Power House zoals Jeroen die in v0.50.0 heeft
losgetrokken uit de YAML. Alles hier is vrij van ESPHome en wordt host-getest
onder `tests/host/`.

| Bestand | Herkomst | Status |
|---|---|---|
| `control/oq_power_house_demand_logic.h` | upstream `control/` | ongewijzigd |
| `control/oq_power_house_dispatch_logic.h` | upstream `control/` | ongewijzigd |
| `control/oq_hp_candidate_logic.h` | upstream `control/` | ongewijzigd |
| `control/oq_heat_intent_logic.h` | upstream `control/` | ongewijzigd |
| `performance/hp_perf_frequency.h` | upstream `performance/` | aangepast: alleen V1/V1.5, gebruikt de eigen `hp_perf_map.h` |
| `odu/oq_odu_variant.h` | upstream `odu/oq_odu_generation.h` | uitgedund tot de enum |

**Ongewijzigd betekent ongewijzigd.** Fork-eigen gedrag (de v0.32-uitbreidingen,
standbegrenzing, de aansluiting op de ODU-frequentietabel) hoort in de adapter,
niet hier. Zo blijft een nieuwe upstream-versie naast deze bestanden te leggen.

Wat bewust NIET is overgenomen: het V2-prestatiemodel, de V2-standvertaling met
21 niveaus, de generatiedetectie, de incident manager en de externe
warmtevraag. Deze fork draait op V1.5 (R32).

Stap 1: alleen de logica, zonder gebruik — er verandert niets aan het
gedrag. Stap 2: een schaduwberekening die deze logica naast de huidige Power House
laat meerekenen en publiceert wat hij zou kiezen, zonder iets aan te sturen.
