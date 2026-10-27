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

## De sonde: wat zijn die slots eigenlijk (v0.67, EXPERIMENTEEL)

Op deze firmware lezen alle twintig slots **nul**. De unit antwoordt wel — bij
een niet-bestaand adres zou er een foutcode komen en zou de status "onbekend"
melden. Twee verklaringen blijven over: de registers bestaan en zijn leeg, óf de
unit beantwoordt elk adres met nullen.

Daarvoor is er een sonde. Die zet **één** register tegelijk op een waarde, leest
terug wat de unit ervan maakte, en zet de oude waarde daarna weer terug.

| Entiteit | Rol |
|---|---|
| `EXPERIMENTEEL slot schrijven vrijgeven` | uit na elke herstart |
| `EXPERIMENTEEL slot` | bladadres 3051 t/m 3070 |
| `EXPERIMENTEEL slotwaarde` | 0 t/m 100 |
| `EXPERIMENTEEL slot schrijven` | lezen, onthouden, schrijven, teruglezen |
| `EXPERIMENTEEL slot herstellen` | de onthouden waarde terugzetten |
| `EXPERIMENTEEL slot status` | `3055: was 0, geschreven 1, teruggelezen 1 - vastgehouden` |

**Aanbevolen volgorde**

1. Schrijf `0` over de bestaande `0`. Dat verandert niets en laat zien of de unit
   de schrijfopdracht op dat adres aanneemt.
2. Schrijf `1` en lees terug. Blijft het staan, dan zijn de registers echt en
   beschrijfbaar. Komt er `0` terug, dan negeert de unit ze.
3. Herstellen — of twee minuten wachten, dan gebeurt het vanzelf.

De uitlezing `ODU slots 3051-3060` leest normaal één keer per uur. Na een proef
wordt die lezing meteen opnieuw gedaan, dus binnen een paar seconden zie je daar
alle twintig slots vers van de unit — ook de negentien die je niet hebt
aangeraakt. Na het herstellen gebeurt hetzelfde.

**Uitkomst op fw 1.30 (V1.5):** slot 3052 nam `95` aan en gaf `95` terug. De
registers zijn dus echte, beschrijfbare schaduwopslag. Dat ze alle twintig op nul
staan terwijl de unit normaal draait, wijst erop dat deze firmware er niets uit
leest — een parameter die in gebruik is, heeft zelden nul als waarde. Zeker weten
doe je dat pas als je een geschreven waarde ergens in het gedrag terugziet.

**Waarom dit voorzichtiger is dan de frequentietabel**

Bij de ladder weten we wat elk getal betekent. Hier niet: op R32 dragen deze
bytes andere parameters, en een schrijfactie in dit blok werkt meteen door —
het is de schaduw waar de regeling uit leest. Daarom:

- de compressor moet stilstaan, **zonder** de "tijdens bedrijf"-ontsnapping die
  de frequentietabel wel heeft;
- waarden blijven tussen 0 en 100;
- na twee minuten gaat de oorspronkelijke waarde er vanzelf weer in;
- niet tijdens een EEPROM-dump.

**Register 5000 komt er niet in voor.** Dat is het vastlegcommando richting
EEPROM. Deze sonde schrijft uitsluitend in de vluchtige schaduw, zodat een power
cycle van de buitenunit altijd alles terugzet. Een host-test bewaakt dat 5000
nooit als geldig sondeadres kan gelden, ook niet als iemand het bereik later
oprekt.

En nog een keer, omdat het telt: als die slots waardes vasthouden, heb je
daarmee nog steeds geen stand 11 t/m 20. De klem zit in de flash.

## Wat er niet in zit

De **live stapindex** — welke stand de unit op dit moment werkelijk draait,
volgens zijn eigen RAM. Die vraagt om de geparkeerde debugvenster-lezer. Zolang
die er niet is, staat hier geen halve versie van.
