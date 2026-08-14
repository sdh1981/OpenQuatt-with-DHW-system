# DHW-rendement en tapdetectie (v0.54)

Twee metingen die tot nu toe ontbraken: wat een warmwatercyclus werkelijk kost en oplevert, en of er water uit de tank loopt.

## Waarom dit er is

Alle keuzes in de DHW-keten — single-HP mode of duo, het compressorniveau, de level bump, wanneer de tweede unit bijschakelt — zijn tot nu toe op redenering afgesteld. Er was geen kWh in en geen kWh uit, dus geen manier om te toetsen of een verandering echt iets opleverde.

De tapdetectie hangt daaraan vast: zonder te weten wanneer er getapt wordt, kun je een afkoelende tank niet onderscheiden van een tank waar iemand uit staat te douchen.

## Cyclusrendement

### Wat er gemeten wordt

| | Bron |
|---|---|
| **Warmte eruit** | `hp1_heat_power` + `hp2_heat_power` — flow × ΔT over de watermeting, geldt ook in werkmodus 3 (Hot water) |
| **Elektrisch erin** | `hp1_power_input` + `hp2_power_input` |
| **Element** | 3 kW, in beide richtingen |

Het HP-deel is dus een echte meting en geen interpolatie uit de perf_map. Alleen het element is een nominale waarde; dat is een schakelaar, geen meting.

Beide worden elke 2 s geïntegreerd zolang er een cyclus loopt (`state != IDLE_CV`). Bij de start gaan de tellers op nul, bij terugkeer naar idle worden ze vastgelegd.

### Het element telt in beide richtingen even zwaar

Dat is geen fout maar het punt: 3 kW erin, 3 kW eruit is COP 1. Een cyclus waarin het element veel werk doet zakt daardoor zichtbaar. Zie je de cyclus-COP structureel richting 1,5 zakken, dan doet het element meer dan bedoeld.

### Entiteiten

| Entiteit | Wat |
|---|---|
| `sensor.openquatt_dhw_cyclus_cop` | warmte eruit ÷ elektrisch erin, laatste voltooide cyclus |
| `sensor.openquatt_dhw_cyclus_energie_in` | Wh elektrisch |
| `sensor.openquatt_dhw_cyclus_energie_uit` | Wh thermisch |

> **Etmaal- en lifetime-cijfers staan elders.** `oq_smart_diagnostics.yaml` houdt DHW-energie en -COP al bij over 24 uur en over de hele levensduur: `DHW COP 24h`, `DHW COP lifetime`, `DHW energy input 24h`, `DHW heat output 24h`, plus het aantal cycli en de gemiddelde kWh per cyclus.
>
> Dit bestand voegt daar alleen de **COP van de laatste voltooide cyclus** aan toe — dat ontbrak. In v0.54 stonden hier ook dag-totalen; die dupliceerden de bestaande 24-uurssensoren en zijn in v0.59 verwijderd.

Een cyclus telt pas mee vanaf **50 Wh**. Daaronder is het een afgebroken start of een klep die kort omging — daar valt niets zinnigs over te zeggen.

De dagtellers resetten op dagnummer, niet op uptime, dus een reboot midden op de dag gooit ze niet weg.

### Waar je het voor gebruikt

Vergelijk cyclussen onder vergelijkbare buitentemperatuur. Nuttige experimenten:

- single-HP mode aan versus uit
- `DHW HP level` een stap hoger of lager
- de assist eerder of later laten inschakelen
- `DHW boost after HP` aan of uit — dat is direct zichtbaar in hoeveel het element bijdraagt

## Tapdetectie

### Hoe het werkt

De daalsnelheid van de tanktop over een venster van 60 s, gemeten terwijl er **niet** verwarmd wordt. Standby-verlies zit rond 0,02 K/min bij een goede tank; een douche haalt makkelijk 1 K/min. Dat is ruim te scheiden.

- **Tapping actief** zodra de daling onder `DHW tapdetectie drempel` komt (default **0,4 K/min**)
- **Loslaten** pas na twee rustige minuten, zodat een korte pauze tussen twee kranen niet als twee tappingen telt
- Tijdens verwarmen zegt de tanktop niets over tappen, dus dan staat de detectie stil

### Dit repareert ook de standby-loss-lerer

De detectie bestond al, maar zat binnen `if (oq_dhw_adaptive_usage_enable)` — een schakelaar die standaard **uit** staat. Gevolg: `oq_dhw_last_tap_event_ms` werd nooit gezet, en de standby-loss-lerer, die meetvensters tot een uur na de laatste tapping overslaat, dacht dus altijd dat het rustig was geweest.

Een douche midden in zo'n venster ging daardoor als tankverlies de UA-schatting in — en die UA voedt weer de time-to-ready en de legionella-ETA.

De detectie staat nu op zichzelf, met een eigen schakelaar die standaard aan staat. De usage-learning gebruikt de tappingen nog steeds voor haar uurpatroon, maar is er niet langer de voorwaarde voor.

### Entiteiten

| Entiteit | Wat |
|---|---|
| `binary_sensor.openquatt_dhw_tapping_actief` | loopt er nu water uit |
| `sensor.openquatt_dhw_tappingen_vandaag` | teller, reset op dagnummer |
| `sensor.openquatt_dhw_laatste_tapping_energie` | ruwe schatting in Wh |
| `sensor.openquatt_dhw_tanktop_daalsnelheid` | de gemeten K/min, om de drempel op af te stellen |
| `switch.openquatt_dhw_tapdetectie` | default aan |
| `number.openquatt_dhw_tapdetectie_drempel` | default 0,4 K/min |

### De energie per tapping is een ondergrens

De schatting is `(tanktop bij start − tanktop bij eind) × tankinhoud × 1,16 Wh/(L·K)`. Door stratificatie koelt de tanktop minder af dan de gemiddelde tankinhoud, dus de werkelijke onttrekking ligt hoger. Bruikbaar om tappingen onderling te vergelijken, niet als absolute meting.

### Afstellen

Kijk naar `sensor.openquatt_dhw_tanktop_daalsnelheid` tijdens een bekende douche en tijdens een rustige nacht. Zit er ruim een orde tussen — en dat hoort — dan mag de drempel gerust op 0,4 blijven staan. Ziet je tank veel korte tappingen die je niet geteld wilt hebben, zet hem hoger.
