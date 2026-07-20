# Heating Strategy

Deze pagina geeft het overzicht van de drie verwarmingsstrategieën in OpenQuatt:

- `Power House`
- `Water Temperature Control`
- `Adaptive Heating`

Het doel is eenvoudig: je snel laten begrijpen **welke strategie wat doet**, **wanneer welke aanpak past**, en **waar de belangrijkste verschillen zitten**.

## Wat is een Heating Strategy?

De `Heating Strategy` bepaalt hoe OpenQuatt van meetwaarden en warmtevraag naar een bruikbare verwarmingsvraag komt.

Dat gebeurt dus nog **voor** de verdeling over compressorlevels en nog **voor** de keuze tussen één of twee warmtepompen.

In gewone taal:

- de strategie bepaalt **hoeveel warmte** OpenQuatt denkt nodig te hebben;
- daarna bepaalt heat-control **hoe** die warmtevraag door de warmtepomp(en) geleverd wordt.

## De drie strategieën

### 1. Power House

`Power House` denkt vooral vanuit de woning als geheel.

Belangrijke kenmerken:

- werkt met buitentemperatuur, kamertemperatuur en setpoint;
- gebruikt een huismodel als basis;
- corrigeert daarop met comfortband en kamerafwijking;
- werkt met `P_house` en `P_req` in watt;
- gebruikt een response profile met opbouw- en afbouwtijd;
- kiest bij `Duo` automatisch de zuinigste geldige Single- of Duo-combinatie.

Deze strategie past vaak goed als je:

- comfort en kamertemperatuur centraal wilt zetten;
- rustig, langer gedrag wilt;
- wilt dat OpenQuatt meer zelf beslist hoe de warmtevraag wordt ingevuld.

Meer detail:

- [Power House](power-house.md)

### 2. Water Temperature Control

`Water Temperature Control` denkt vooral vanuit de gewenste aanvoertemperatuur.

Belangrijke kenmerken:

- gebruikt een stooklijn op basis van buitentemperatuur;
- bepaalt daarmee een `Heating Curve Supply Target`;
- gebruikt een PID-regelaar om de gemeten aanvoer die doeltemperatuur te laten volgen;
- vertaalt de PID-uitkomst naar demand `0..20`;
- werkt bij `Duo` in de basis single-HP-first met hysterese voor de tweede unit.

Deze strategie past vaak goed als je:

- liever met aanvoertemperatuur of stooklijn werkt;
- het systeem meer als klassieke CV-/aanvoerregeling wilt benaderen;
- de warmtepomp primair op watertemperatuur wilt laten sturen.

Meer detail:

- [Water Temperature Control](water-temperature-control.md)

### 3. Adaptive Heating

`Adaptive Heating` is `Water Temperature Control` met een zelflerende offset erbovenop.

Belangrijke kenmerken:

- gebruikt dezelfde stooklijn, PID en Duo-opbouw als `Water Temperature Control`;
- legt daar een geleerde offset overheen op het `Heating Curve Supply Target`;
- zoekt de laagst mogelijke aanvoertemperatuur waarbij de kamer het setpoint nog haalt;
- leert langzaam omlaag en sneller omhoog — comfort gaat voor rendement;
- pauzeert het leren bij koelen, DHW-prioriteit, defrost en kort na een setpoint-wijziging;
- bewaart de geleerde offset over een herstart.

Deze strategie past vaak goed als je:

- met een stooklijn wilt werken maar niet handmatig wilt blijven bijstellen;
- rendement en compressorbelasting als belangrijkste doelen hebt;
- een betrouwbare kamertemperatuurmeting hebt.

Meer detail:

- [Adaptive Heating](adaptive-heating.md)

## Belangrijkste verschil

De kern is:

- `Power House` denkt eerst in **warmtevraag van het huis**
- `Water Temperature Control` denkt eerst in **gewenste aanvoertemperatuur**
- `Adaptive Heating` denkt in **de laagste aanvoertemperatuur die nog volstaat**

Dat verschil werkt door in bijna alles:

- welke signalen het zwaarst meewegen;
- hoe de regeling op afwijkingen reageert;
- welke instellingen het belangrijkst zijn;
- hoe `Single` en `Duo` zich in de praktijk gedragen.

## Welke strategie past wanneer?

### Kies eerder `Power House` als:

- je kijkt naar kamercomfort en warmtevraag van de woning;
- je liever niet te veel in een stooklijn denkt;
- je wilt dat OpenQuatt bij `Duo` efficiënt tussen single en duo kiest;
- je gedrag wilt afstellen met huismodel, comfortband, temperatuurreactie en response profile.

### Kies eerder `Water Temperature Control` als:

- je gewend bent te werken met een stooklijn;
- je aanvoertemperatuur en watergedrag centraal wilt zetten;
- je liever eerst de curvepunten en PID instelt;
- je een meer klassieke verwarmingsaanpak wilt.

### Kies eerder `Adaptive Heating` als:

- je stooklijn draait, maar je vermoedt dat hij te hoog staat;
- je rendement wilt winnen zonder er zelf steeds aan te draaien;
- je de persgastemperatuur en frequentiebegrenzing van je R32-units wilt ontlasten;
- je kamertemperatuurmeting betrouwbaar is en niet door zon of open ramen wordt verstoord.

## Wat verandert niet per strategie?

Welke strategy je ook kiest:

- bronkeuze blijft belangrijk;
- flow en veiligheidslogica blijven actief;
- `Control Mode` blijft leidend voor wanneer verwarmen mag;
- de gedeelde watertempbegrenzing blijft bestaan;
- `CM3`-logica en ketelhulp blijven erboven liggen.

Dus: de strategie is belangrijk, maar niet het enige wat het gedrag bepaalt.

## Hoe kies je verstandig?

Een veilige aanpak is:

1. begin met de strategie die het beste past bij hoe jij naar je systeem kijkt;
2. beoordeel eerst bronwaarden, flow en algemeen gedrag;
3. stel daarna pas de strategie-specifieke parameters bij;
4. wissel niet te snel heen en weer tussen strategieën tijdens één korte testperiode.

## Samenvatting

Als je het kort wilt onthouden:

- `Power House` = woning- en comfortgericht
- `Water Temperature Control` = aanvoer- en stooklijngericht
- `Adaptive Heating` = stooklijn die zichzelf zo laag mogelijk afstelt

Geen van beide is "altijd beter". Het hangt af van:

- je installatie;
- je huis;
- je manier van afstellen;
- en wat je als prettig en logisch gedrag ervaart.

## Gerelateerde pagina's

- [Power House](power-house.md)
- [Water Temperature Control](water-temperature-control.md)
- [Heating Strategy Development](heating-strategy-development.md)
- [Hoe OpenQuatt werkt](hoe-openquatt-werkt.md)
- [Regelgedrag van OpenQuatt](regelgedrag-van-openquatt.md)
