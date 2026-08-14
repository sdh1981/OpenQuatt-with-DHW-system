# Compressor cycling monitor (v0.58)

Telt compressorstarts per HP en waarschuwt bij te veel starts of bij afwisselend pendelen tussen de units.

Overgenomen uit [OpenQuatt/OpenQuatt](https://github.com/OpenQuatt/OpenQuatt). De rekenlogica in `openquatt/includes/oq_installation_monitoring.h` is verbatim overgenomen — dat is afhankelijkheidsvrije C++ en zo blijft hij vergelijkbaar met de bron. De ESPHome-laag eromheen is opnieuw geschreven voor deze fork.

## Waarom dit hier zit

Deze fork heeft twee mechanismen die pendelen moeten voorkomen:

- de **lead-lock** in single-HP DHW-mode, die de vraag per cyclus aan één unit vastzet
- de **minimale draaitijd en lockout** van de tweede-HP assist

Er was geen enkele meting die liet zien of dat werkt. Deze monitor levert die.

## Wat er gemeten wordt

Starts worden afgeleid uit de **gemeten compressorfrequentie**, niet uit het niveau dat OpenQuatt commandeert. Dat onderscheid is het punt: een unit kan zichzelf afschakelen op een interne beveiliging zonder dat de regeling daar iets van weet. De frequentie laat dat wel zien.

Per HP tellers over **2, 6, 24 en 72 uur**, plus de leeftijd van de laatste start in minuten.

Twee details in de header die het bruikbaar maken:

| Constante | Wat het doet |
|---|---|
| `kStopConfirmMs` (20 s) | een korte frequentiedip telt niet als stop gevolgd door herstart |
| `kMaxObservationGapMs` | na een reboot of OTA ontstaan er geen spookstarts |

## De twee waarschuwingen

### Te veel starts

Vuurt zodra een van beide units boven een drempel komt.

| Drempel | Default | Upstream |
|---|---|---|
| `Cycling waarschuwing 2u` | **8** | 6 |
| `Cycling waarschuwing 72u` | **50** | 40 |

Hoger dan upstream, en met reden: daar telt alleen verwarmen mee. Hier komt een DHW-cyclus bovenop die de compressor apart start, plus de assist die de tweede unit in- en uitschakelt. Zes starts per twee uur haal je op een tapdag zonder dat er iets mis is.

Dit zijn startwaarden. Kijk na een week naar `HP1/HP2 starts 24u` en stel bij op wat je werkelijk ziet.

### Afwisselend pendelen

Aparte melding, want het wijst op iets anders. Vuurt bij **vier of meer opeenvolgende starts binnen twee uur die steeds van unit wisselen** — HP1, HP2, HP1, HP2.

```cpp
if (previous_hp != 0 && previous_hp == event.hp_index) return false;
```

Twee starts van dezelfde unit achter elkaar breken het patroon. Dit is dus specifiek het geflipper waar de lead-lock voor gebouwd is, en niet gewoon "vaak starten".

Ziet u `binary_sensor.openquatt_cycling_afwisselend_pendelen` aangaan, kijk dan naar `sensor.openquatt_dhw_single_hp_lead`: die zou binnen een DHW-cyclus niet moeten wisselen.

## Gelatchte incidenten

Een waarschuwing blijft staan tot je hem bevestigt met `button.openquatt_cycling_waarschuwing_bevestigen`. Zonder dat zou een incident 's nachts ongezien voorbijgaan.

Zolang het incident openstaat worden de **pieken** bijgehouden: `Cycling piek 2u` en `Cycling piek 72u`. Die kun je nog aflezen nadat de lopende tellers alweer gezakt zijn — anders zie je alleen dat er iets was, niet hoe erg.

`sensor.openquatt_cycling_status` vat het samen: `Geen incident`, of bijvoorbeeld `Afwisselend pendelen - piek 11/2u, 47/72u`.

## Entiteiten

| Entiteit | Wat |
|---|---|
| `sensor.openquatt_hp1_starts_2u` / `_24u` / `_72u` | tellers HP1 |
| `sensor.openquatt_hp2_starts_2u` / `_24u` / `_72u` | tellers HP2 |
| `sensor.openquatt_hp1_laatste_start` / `hp2` | minuten sinds de laatste start |
| `binary_sensor.openquatt_cycling_waarschuwing` | gelatcht |
| `binary_sensor.openquatt_cycling_afwisselend_pendelen` | gelatcht |
| `sensor.openquatt_cycling_status` | samenvatting |
| `sensor.openquatt_cycling_piek_2u` / `_72u` | pieken tijdens het incident |
| `number.openquatt_cycling_waarschuwing_2u` / `_72u` | drempels |
| `button.openquatt_cycling_waarschuwing_bevestigen` | wist incident en pieken |

Alles staat samen op de sectie **Compressor cycling** van de diagnostiektab.
