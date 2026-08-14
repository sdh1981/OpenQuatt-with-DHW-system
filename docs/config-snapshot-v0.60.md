# Instellingen-vangnet (v0.60)

Leg vast wat jouw goede instellingen zijn, en krijg te horen wanneer de werkelijkheid daarvan afdrijft.

## Waarom dit bestaat

`DHW single HP level bump` stond op 2 terwijl de default 1 is en geen enkele regel code die waarde schrijft. Dat kwam pas aan het licht toen een compressor op level 4 bleek te draaien, en is toen van achteren naar voren teruggerekend.

Het probleem was niet dat de waarde veranderde. Het probleem was dat er **geen manier was om dat te zien**.

Home Assistant registreert al die entiteiten wel degelijk in zijn recorder — de historie van die bump stond er gewoon, met tijdstip en al. Het gat zit niet in registratie maar in **opmerken** en **terugkomen**.

## Geen vergelijking met de fabrieksdefaults

Die zouden permanent afwijken: er staan bewust tientallen dingen anders dan standaard. Een sensor die altijd "wijkt af" zegt niets.

In plaats daarvan leg je zelf een ijkpunt vast. Vanaf dat moment meldt het systeem wat er ten opzichte van *jouw* keuzes verandert.

## Gebruik

| Entiteit | Wat het doet |
|---|---|
| `button.openquatt_instellingen_vastleggen` | neemt een momentopname van alle instellingen in scope |
| `binary_sensor.openquatt_instellingen_gewijzigd` | aan zodra er iets afwijkt |
| `sensor.openquatt_instellingen_afwijking` | noemt wélke, met oude en nieuwe waarde |
| `sensor.openquatt_instellingen_afwijkingen` | hoeveel er afwijken |
| `sensor.openquatt_instellingen_vastgelegd` | hoeveel er zijn vastgelegd |
| `button.openquatt_instellingen_herstellen` | schrijft de momentopname terug |

Druk op **vastleggen** zodra je installatie staat zoals je hem wilt. Verander je daarna bewust iets, leg dan opnieuw vast — anders blijft de melding staan.

Bij het level-4-voorval had er gestaan:

```
dhw_single_hp_level_bump: 1 -> 2
```

## Scope

83 entiteiten: alle config-numbers en -switches met de prefix `dhw_`, `pressure_`, `supply_temp_` of `discharge_`. Dat is de DHW-keten plus de drie beveiligingsmodules — alles wat dit jaar daadwerkelijk voor verrassingen heeft gezorgd.

Op prefix in plaats van een handmatige lijst, zodat nieuwe instellingen vanzelf meelopen. Power House en cooling zitten er bewust niet in; die kunnen er later bij als de melding niet te veel ruis geeft.

## Waarom op hash en niet op volgorde

De momentopname bewaart per instelling `get_object_id_hash()`, niet de positie in `App.get_numbers()`. Die volgorde is de registratievolgorde en verschuift zodra er entiteiten bijkomen of verdwijnen — precies het scenario waar dit vangnet voor bedoeld is.

Op hash opzoeken blijft kloppen: instellingen die na de vastlegging zijn toegevoegd hebben simpelweg geen vermelding, verdwenen instellingen laten een ongebruikte vermelding achter.

## Wat dit niet is

De momentopname leeft in dezelfde preference-opslag die hier verdacht is. Verschuiven de slots, dan kan de opname meeschuiven.

Daarom staat alles in **één** global in plaats van 83 losse: één slot in plaats van 83, en raakt die corrupt dan zie je onzin in plaats van geloofwaardig-maar-fout.

Dit maakt drift zichtbaar en terugdraaibaar. Het voorkomt hem niet. Voorkomen zou betekenen `restore_value` opgeven, en dat kost meer dan het oplevert.
