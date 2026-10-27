# ODU-compressorstanden en hun plafond (v0.66)

Alleen lezen. Deze feature laat zien welke frequentieladder je buitenunit nu
gebruikt, wat er in de slots 3051-3070 staat, en waar het stapplafond vandaan
komt. Hij verandert niets aan de regeling en schrijft niets naar de unit.

## Waarom F11 t/m F20 niet aan te zetten zijn

Op R32 klemt de regelprint de compressorstand op tien. Die grens staat in de
**programmaflash** van de STM8, als een vaste vergelijking — niet in de EEPROM
en niet in de schaduwregisters die een Modbus-master kan schrijven.

Daar volgt alles uit:

- Er bestaat **geen register** dat stand 11 t/m 20 vrijgeeft. Zoeken in de
  schaduw levert niets op, hoe vaak je ook kijkt.
- Vrijgeven vraagt om het patchen van die flash en de unit opnieuw programmeren.
  Dat valt **buiten OpenQuatt**: een Modbus-master schrijft de schaduw, niet de
  programmaflash.
- De slots op bladadres 3051-3070, die op andere platformen de F11-F20-ladder
  zijn, dragen op R32 **andere parameters**. Ze worden hier daarom rauw en
  decimaal getoond, zonder eenheid, en nadrukkelijk niet als frequentie.

Die laatste regel wordt bewaakt door een host-test: de slotweergave mag het
woord "Hz" niet bevatten.

## Wat je ziet, per unit

| Entiteit | Wat erin staat |
|---|---|
| `ODU ladder verwarmen` | `0,30,39,49,55,61,67,72,79,85,90 Hz (F0-F10, 5 min oud)` |
| `ODU ladder koelen` | dezelfde vorm, uit dezelfde lezing |
| `ODU slots 3051-3060 (geen frequenties)` | tien rauwe registerwaarden, decimaal |
| `ODU slots 3061-3070 (geen frequenties)` | idem |
| `ODU stapplafond` | `10` standen, vast |
| `ODU standen status` | `R32 fw 1.30 - plafond 10 standen (firmware, niet instelbaar)` |

Alles staat in de webgroep bij de frequentietabel en is `diagnostic`.

**Let op het verschil met de invoervakken.** De bestaande
`EXPERIMENTEEL koelen/verwarmen Fx Hz` zijn *invoervelden* die onthouden wat jij
hebt ingetypt. De ladder hierboven is wat er werkelijk in de unit staat.

## Versiepoort

Alles hangt aan holding 2122, die `oq_HP_io.yaml` al leest en decodeert. Alleen
op de twee bekende R32-versies — **1.25** (V1) en **1.30** (V1.5) — gaat er iets
de bus op. Op elke andere firmware blijft het stil en melden de entiteiten
"niet van toepassing".

Bewust die twee waarden en niet "major == 1": een onbekende R32-versie hoort
eerlijk te melden dat dit niet van toepassing is, in plaats van stilzwijgend aan
te nemen dat het plafond daar ook tien is.

## Waar de adressen vandaan komen

Uit de eigen registerkaart, niet uit extern materiaal.
[oq_odu_runtime_frequency_table_logic.h](../openquatt/includes/oq_odu_runtime_frequency_table_logic.h)
legt de regel vast: *Modbus-adres is bladadres min 1*, met koelen F0-F10 op blad
3001..3011 (modbus 3000..3010) en verwarmen F0-F10 op blad 3012..3022 (modbus
3011..3021). Dezelfde regel geeft voor de slots: blad 3051..3070 → **modbus
3050..3069**.

De firmwareversie komt uit holding 2122 zoals
[oq_HP_io.yaml](../openquatt/oq_HP_io.yaml) die al leest: hoge byte major, lage
byte minor.

## Busverkeer

Eén lezing van twintig registers per unit per uur, dertig seconden na het
opstarten voor het eerst. Overgeslagen zolang er een EEPROM-dump loopt. Na drie
mislukte pogingen wacht hij tot de volgende ronde; de teller blijft staan, want
die is de diagnose.

De ladderlezing die hier ook wordt aangestopt bestond al. Op een Duo deed de
Power House v0.50-package dat al elk kwartier; op een enkele unit deed niemand
het, en bleef die tabel leeg.

## Wat er niet in zit

De **live stapindex** — welke stand de unit op dit moment werkelijk draait,
volgens zijn eigen RAM. Die vraagt om de geparkeerde debugvenster-lezer. Zolang
die er niet is, staat hier geen halve versie van.
