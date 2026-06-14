# CWT-TM-8PT — 8-kanaals PT1000 module (v0.40)

## Inleiding

Vanaf v0.40 wordt de externe ESP32 Dallas-bridge (DS18B20 sensoren via Modbus
op bus 3) vervangen door een **CWT-TM-8PT** module van ComWinTop:

- **8 PT100/PT1000 kanalen** (per kanaal te configureren)
- **Modbus RTU** over RS485
- **0.1°C resolutie**, **0.25°C nauwkeurigheid**
- **Bereik −180 tot +650°C**
- Industriële kwaliteit, DIN-rail montage

Voordelen t.o.v. de oude setup:
- 8 kanalen i.p.v. 2 → 4 spare voor toekomstige sensoren
- Hogere precisie (0.1°C vs 0.5°C voor DS18B20 standaard)
- Externe ESP32 + Dallas-node kan eruit (minder onderhoud)
- Lokale MAX31865 chips zijn nu **backup** i.p.v. primaire bron

---

## Hardware specificatie

| Eigenschap | Waarde |
|------------|--------|
| Model | ComWinTop CWT-TM-8PT |
| Voeding | DC 8–30 V |
| Verbruik | 9 mA @ 30 V, 12 mA @ 24 V, 23 mA @ 12 V, 33 mA @ 8 V |
| Sensor types | PT100 of PT1000 |
| Aansluitingen | 2-draads of 3-draads |
| Detectie | Disconnect en short-circuit |
| Communicatie | RS485, Modbus RTU |
| Default config | **9600 8N1**, slave ID **1** |
| Configureerbaar | Baud (1200..19200), parity, slave ID (1..250) |
| Montage | 35mm DIN-rail |
| Behuizing | ABS |
| Afmetingen | 88 × 72 × 59 mm |

---

## Aansluitschema

### Klemmenstrook (linkse rij)
```
Klemnr  Functie
─────  ───────────────
 1     GND  (sensor GND)
 2     RTD1+ (PT1000 #1 leiding 1)
 3     RTD1- (PT1000 #1 leiding 2)
 4     GND
 5     RTD2+
 6     RTD2-
 7     GND
 8     RTD3+
 9     RTD3-
10     GND
11     RTD4+
12     RTD4-
13     GND
14     RTD5+
15     RTD5-
16     GND
```

### Klemmenstrook (rechtse rij)
```
Klemnr  Functie
─────  ───────────────
 1     GND
 2     RTD6+
 3     RTD6-
 4     GND
 5     RTD7+
 6     RTD7-
 7     GND
 8     RTD8+
 9     RTD8-
10     +V    (voeding +)
11     GND   (voeding -)
12     A(D+) (RS485 +)
13     B(D-) (RS485 -)
```

### Op de LilyGo CIC

- **RS485 #3 TX** → CWT klem A (D+) (via RS485 transceiver)
- **RS485 #3 RX** → CWT klem B (D-)
- **+V** → 12V of 24V DC voeding
- **GND** → gemeenschappelijk

LilyGo CIC GPIO mapping:
- `dhw_rs4853_tx_pin`: GPIO17
- `dhw_rs4853_rx_pin`: GPIO18

---

## Kanaal-toewijzing

Default mapping in OpenQuatt v0.40:

| Kanaal | Functie | Sensor type | Locatie |
|--------|---------|-------------|---------|
| **1** | DHW Tank Top | PT1000 2-draads | Bovenste dompelhuls in tank |
| **2** | DHW Tank Bottom | PT1000 2-draads | Onderste dompelhuls in tank |
| **3** | DHW Coil In | PT1000 2-draads | Spiraal-inlaat (uit warmtepomp) |
| **4** | DHW Coil Out | PT1000 2-draads | Spiraal-uitlaat (terug naar warmtepomp) |
| **5** | Spare | n.t.b. | Optioneel — bv. buffer top |
| **6** | Spare | n.t.b. | Optioneel — bv. buffer bottom |
| **7** | Spare | n.t.b. | Optioneel — bv. vloer-aanvoer |
| **8** | Spare | n.t.b. | Optioneel — bv. vloer-retour |

Spare kanalen zijn standaard **disabled_by_default: true** zodat ze niet in
HA verschijnen tot je ze nodig hebt. Activeer per kanaal via:

`Settings → Devices & Services → ESPHome openquatt → Entities → CWT ChN`
→ Enable

---

## Modbus register map

OpenQuatt gebruikt **UINT16 met scale 0.1** (compact, snel, voldoende precisie):

| Kanaal | Register adres (hex) | Adres (dec) | Format | Voorbeeld waarde |
|--------|---------------------|-------------|--------|------------------|
| Ch1 | 0x68 | 104 | S_WORD × 0.1 | 222 → 22.2°C |
| Ch2 | 0x69 | 105 | S_WORD × 0.1 | 223 → 22.3°C |
| Ch3 | 0x6A | 106 | S_WORD × 0.1 | ... |
| Ch4 | 0x6B | 107 | S_WORD × 0.1 | |
| Ch5 | 0x6C | 108 | S_WORD × 0.1 | |
| Ch6 | 0x6D | 109 | S_WORD × 0.1 | |
| Ch7 | 0x6E | 110 | S_WORD × 0.1 | |
| Ch8 | 0x6F | 111 | S_WORD × 0.1 | |

> S_WORD = signed 16-bit, ondersteunt negatieve waarden (raw -100 = -10.0°C)

Float32 alternatief beschikbaar op `0x30` t/m `0x3E` voor maximale precisie.
Wij gebruiken dat niet om Modbus-traffic beperkt te houden (8 × 2 bytes
i.p.v. 8 × 4 bytes per poll).

---

## Migratie vanaf oude bridge

### Vóór v0.40 (oude setup)
```
[Tank top sensor]──DS18B20──[Externe ESP32]──Modbus RTU──[LilyGo CIC]
[Tank bottom]   ──DS18B20──┘
[Coil in]      ──PT1000──[lokale MAX31865]
[Coil out]     ──PT1000──[lokale MAX31865]
```

### Vanaf v0.40 (nieuwe setup)
```
[Tank top]──PT1000──┐
[Tank bottom]──PT1000──┤
[Coil in]──PT1000──┼──[CWT-TM-8PT]──Modbus RTU 9600 8N1──[LilyGo CIC]
[Coil out]──PT1000──┘
[Spare 1-4]──optioneel──┘
```

### Bron-prioriteit (fallback chain)

Voor elke DHW temperatuur-input geldt deze volgorde — eerste geldige wint:

```
1. CWT-TM-8PT      (Ch1..Ch4)           ← primary
2. Lokale MAX31865 (alleen coil_in/out) ← backup
3. HA-sensor       (cloud / handmatig)   ← fallback
4. Local override  (slider voor testen)  ← last resort
```

De **lokale MAX31865 chips blijven** in de firmware voor zekerheid: als de
CWT module faalt, neemt MAX31865 over voor coil_in/out. Voor tank top/bottom
is er geen backup beschikbaar — daar moet HA-sensor of override gebruikt
worden.

---

## Eerste opstart procedure

### 1. Hardware aansluiten
- CWT-module op DIN-rail in de kast
- Voeding 12 of 24 V aansluiten op klem +V en GND
- RS485 via transceiver op LilyGo's `uart_rs485_3`
- 4 PT1000 sensoren aansluiten op kanalen 1–4 (2-draads, GND mee)

### 2. Verifieer module via Modbus tool (optioneel)
Voor flash, gebruik een Modbus-poll tool om te verifiëren:
- Verbinding: 9600 8N1, slave ID 1
- Read holding register 0x68 (= 104) — moet een S_WORD geven met de temperatuur × 10
- Bij 22.2°C → leest 222 (0xDE)

### 3. Flash de nieuwe firmware
- Build OpenQuatt v0.40 met dit YAML
- Flash de LilyGo CIC

### 4. Verifieer in HA
Bij eerste opstart na flash:
- `sensor.openquatt_cwt_ch1_dhw_tank_top` toont temperatuur
- `sensor.openquatt_dhw_tank_top` (final) gebruikt CWT als bron
- Spare kanalen zijn zichtbaar maar disabled by default

### 5. Eventueel oude hardware verwijderen
- Externe ESP32 Dallas-node kan uit het systeem (niet meer nodig)
- Lokale MAX31865 chips kunnen blijven als backup (aanbevolen) of weg
  als je extra GPIO-pinnen wilt vrijmaken

---

## Configuratie van de CWT-module

### Slave ID veranderen
Default = 1. Als je meerdere CWT-modules op één bus wil:
- Stuur Modbus write naar register 0x10 (= 16), HI byte = nieuwe slave ID (1..250)
- Bijv. `0x06 0x01 0x00 0x10 0x02 0x04 [CRC]` voor slave 2, baud 9600

### Sensor type per kanaal
Documentatie van CWT geeft aan dat PT100/PT1000 wordt **gedetecteerd** of
**ingesteld** via configuratie. Voor de **PT1000** versie van het apparaat
is dit standaard ingesteld voor alle 8 kanalen.

### Baud rate veranderen
Default = 9600. Niet aanbevolen — werkt prima zoals het is.
Indien gewenst: schrijf register 0x10 met juiste bits (zie CWT manual).

---

## Storingen en diagnose

### CWT entiteiten tonen "Unknown" of NAN

**Mogelijk oorzaken:**
1. **Voeding ontbreekt** — controleer DC op +V/GND klem
2. **RS485 bekabeling** — A/B omgekeerd? Schermen aangesloten?
3. **Slave ID verkeerd** — controleer dat module op slave 1 staat
4. **Baud rate verkeerd** — module op 9600? UART config in firmware?

### Sensor toont onlogische waarde (bv. -100°C of 600°C)

**Oorzaken:**
1. **Sensor disconnect** — CWT detecteert dit en geeft een speciale code (zie manual)
2. **Short circuit** — idem
3. **Verkeerd sensor-type** — PT100 op PT1000 ingang of vice versa

CWT geeft fault-codes terug in de raw waarde — controleer in de logs welke
waarde wordt teruggelezen voordat de × 0.1 filter wordt toegepast.

### Fallback naar MAX31865 of HA

Als CWT offline gaat, vallen `dhw_coil_in_input` en `dhw_coil_out_input`
automatisch terug op de lokale MAX31865 chips. Voor `dhw_tank_top_input` en
`dhw_tank_bottom_input` is er geen lokale backup — daar wordt eventueel
teruggevallen op HA-sensoren of de local-override sliders.

Controleer in HA:
- `dhw_tank_top_input` toont nog steeds een waarde?
- Welke bron wordt gebruikt? (Niet direct zichtbaar — moet uit context worden afgeleid)

---

## Technische details

### Bron-prioriteit code (Tank Top voorbeeld)

```cpp
// dhw_tank_top_input lambda
if (cwt_pt_ch1_c.has_state() && !isnan(cwt_pt_ch1_c.state)) {
  return cwt_pt_ch1_c.state;           // 1. CWT primair
}
if (dhw_tank_top_ha.has_state() && !isnan(dhw_tank_top_ha.state)) {
  return dhw_tank_top_ha.state;        // 2. HA fallback
}
return oq_dhw_src_tank_top_c.state;    // 3. Local override
```

### Modbus poll cycle

```yaml
modbus_controller:
  - id: cwt_pt_module
    address: 1
    modbus_id: mod_bus_3
    update_interval: 5s        # poll elke 5s
    command_throttle: 80ms     # min tijd tussen commands
    max_cmd_retries: 1         # 1 retry bij timeout
    offline_skip_updates: 2    # na 2 fails → entity = NAN
```

Bij 8 kanalen UINT16 wordt het in **één Modbus-read-call** opgehaald
(adressen 0x68..0x6F, lengte 8). Bandbreedte: ~25 bytes per cycle = 250ms
bij 9600 baud. Ruim binnen de 5-seconden poll-cycle.

---

## Wijzigingsgeschiedenis

| Versie | Wijziging |
|--------|-----------|
| v0.40.0 | **Initial release** — CWT-TM-8PT module integratie, vervangt Dallas-bridge |
