# Adaptive Heating

`Adaptive Heating` is de derde `Heating Control Mode`, naast [Power House](power-house.md) en [Water Temperature Control](water-temperature-control.md).

Kort gezegd: **Adaptive Heating is de stooklijn, maar dan zelflerend**. Hij zoekt automatisch de *laagst mogelijke aanvoertemperatuur* waarbij je kamer het setpoint nog haalt.

## Waarom lagere aanvoer?

Twee redenen, en beide tellen mee:

**Rendement.** Elke graad lagere aanvoertemperatuur levert ruwweg **2-3 % betere COP** op. Dat is de grootste rendementsknop die je hebt — groter dan vrijwel elke andere instelling.

**Slijtage.** Bij R32 loopt de persgastemperatuur snel op naarmate de lift (verschil tussen verdampen en condenseren) groter wordt. Bij hoge aanvoertemperaturen knijpt de buitenunit zichzelf af (zie `Compressor frequency limited` en `Frequency limit reason`). Lagere aanvoer betekent dus lagere persgastemperatuur, minder begrenzing en minder thermische stress op de compressor.

Een stooklijn wordt in de praktijk bijna altijd te hoog ingesteld — je stelt hem af op de koudste dag en op de traagste kamer. Adaptive Heating haalt die marge er geleidelijk uit.

## Hoe het werkt

Adaptive Heating draait op dezelfde tak als `Water Temperature Control`: dezelfde stooklijn, dezelfde PID, dezelfde Duo-opbouw. Het enige verschil is een **geleerde offset** die bovenop het `Heating Curve Supply Target` komt.

Die offset wordt elke minuut bijgewerkt volgens een bewust **asymmetrische** regel:

| Situatie | Wat er gebeurt |
| --- | --- |
| Kamer op of boven setpoint | Offset **langzaam omlaag** (standaard 0,2 K/uur) |
| Kamer onder setpoint (buiten deadband) | Offset **sneller omhoog** (standaard 0,8 K/uur) |
| Binnen de deadband | Offset blijft staan |

Omhoog gaat vier keer zo snel als omlaag: **comfort gaat voor rendement**. Het resultaat is dat de offset langzaam naar beneden kruipt tot je huis het net niet meer bijhoudt, en daar dan in een kleine slingering blijft hangen — precies rond het echte minimum.

De geleerde offset **blijft bewaard over een herstart**. Het is dure kennis over jouw huis; die gooi je niet weg bij een reboot of firmware-update.

## Wanneer wordt er níét geleerd

Leren wordt gepauzeerd zodra de meting niets zegt over de stooklijn:

- **koelbedrijf** (`CM5`);
- **DHW-prioriteit** — de CV wordt dan niet bediend, dus een dalende kamertemperatuur zegt niets over je curve;
- **defrost** — tijdelijk vermogensverlies, geen curveprobleem;
- **kort na een setpoint-wijziging** — je huis reageert traag; zonder die pauze zou de offset op die traagheid wegdrijven (standaard 30 minuten);
- kamertemperatuur of setpoint niet beschikbaar.

Daarnaast geldt: **omhoog corrigeren mag alleen als het systeem ook echt warmte vraagt**. Is de kamer koud terwijl er geen warmtevraag actief is, dan zit het probleem ergens anders en moet de curve niet omhoog.

Je ziet de actuele toestand terug in `Adaptive heating status`: `Uit`, `Gepauzeerd`, `Leren (omlaag)`, `Stabiel` of `Corrigeren (omhoog)`.

## Instellingen

| Instelling | Standaard | Wat het doet |
| --- | --- | --- |
| `Adaptive max trim down` | 8 K | Hoeveel de curve maximaal omlaag mag |
| `Adaptive max trim up` | 4 K | Hoeveel de curve maximaal omhoog mag |
| `Adaptive learn rate down` | 0,2 K/h | Hoe snel hij omlaag zoekt |
| `Adaptive learn rate up` | 0,8 K/h | Hoe snel hij comfort terugpakt |
| `Adaptive room deadband` | 0,2 K | Dode band rond het setpoint |
| `Adaptive minimum supply` | 25 °C | Harde ondergrens voor de aanvoer |
| `Adaptive settle after setpoint change` | 30 min | Leerpauze na een setpoint-wijziging |

Verder is er `Adaptive supply offset` (de huidige geleerde offset, in K) en de knop `Adaptive reset learned offset` om opnieuw te beginnen.

De offset blijft altijd begrensd door de bestaande grenzen: `Adaptive minimum supply` aan de onderkant en `max_water_temp_limit_c` aan de bovenkant. Adaptive Heating kan de veiligheidsketen dus niet omzeilen.

## In gebruik nemen

1. Stel eerst je stooklijn normaal in via `Water Temperature Control` en laat die een paar dagen draaien. Adaptive Heating vertrekt vanaf jouw curve — een slecht startpunt kost alleen maar leertijd.
2. Zet `Heating Control Mode` op `Adaptive Heating`.
3. Laat hem met rust. Met de standaardinstellingen duurt het **enkele dagen tot een week** voordat de offset zijn plek vindt. Dat is opzet: langzaam leren voorkomt dat één koude nacht of één open raam je curve verpest.
4. Volg `Adaptive supply offset`. Zakt die naar bijvoorbeeld −4 K en blijft je kamer op temperatuur, dan stond je stooklijn 4 K te hoog — en draai je nu zo'n 10 % zuiniger.

Loop je tegen comfortproblemen aan, verhoog dan eerst `Adaptive learn rate up` of `Adaptive room deadband` voordat je de trim-grenzen aanpast.

## Wanneer kies je Adaptive Heating?

**Wel** als je met een stooklijn wilt werken maar niet eindeloos handmatig wilt bijstellen, en als rendement en compressorbelasting je belangrijkste doelen zijn.

**Niet** als je kamertemperatuurmeting onbetrouwbaar is of sterk beïnvloed wordt door zon, open ramen of een kachel. De hele regeling hangt op die meting — is die slecht, dan leert Adaptive Heating de verkeerde dingen. Controleer in dat geval eerst je bronkeuze via [Diagnose en afstelling](diagnose-en-afstelling.md).

## Zie ook

- [Heating Strategy](heating-strategy.md) — overzicht van alle drie de modes
- [Water Temperature Control](water-temperature-control.md) — de onderliggende stooklijn en PID
- [Diagnose en afstelling](diagnose-en-afstelling.md) — bronkeuze en afstelvolgorde
