# Split-Flap Unit – Arduino Nano

Dokumentation der Firmware für eine einzelne Split-Flap-Unit. Jede Unit besteht aus einem Arduino Nano, einem 28BYJ-48-Schrittmotor mit Treiber, einem Hall-Sensor, einem I²C-Adressschalter und einer WS2811B/NeoPixel-kompatiblen RGB-LED.

Die Unit wird von einem übergeordneten Controller (Master, z. B. ESP8266) über I²C angesprochen. Der Master sendet die gewünschte Zeichenposition, die Motorgeschwindigkeit oder LED-Farbwerte. Die Unit steuert daraufhin den Motor und aktualisiert die LED.

> **Abgrenzung:** Diese Firmware ist die Firmware einer einzelnen Nano-Unit. WLAN, MQTT, Home Assistant, Nachrichtenwarteschlange und Display-Animationen werden vom Master verwaltet. Der Nano selbst besitzt keine MQTT- oder WLAN-Funktion.

## Inhaltsverzeichnis

- [Split-Flap Unit – Arduino Nano](#split-flap-unit--arduino-nano)
  - [Inhaltsverzeichnis](#inhaltsverzeichnis)
  - [Features](#features)
  - [Hardware und Pinbelegung](#hardware-und-pinbelegung)
    - [RGB-LED](#rgb-led)
    - [Schrittmotor](#schrittmotor)
    - [Hall-Sensor](#hall-sensor)
    - [I²C-Bus](#ic-bus)
  - [I²C-Adresse einstellen](#ic-adresse-einstellen)
  - [Zeichensatz und Positionen](#zeichensatz-und-positionen)
  - [I²C-Protokoll](#ic-protokoll)
    - [1. Zeichen- und Motorbefehl](#1-zeichen--und-motorbefehl)
    - [2. Zeichenbefehl mit Kalibrier-Offset](#2-zeichenbefehl-mit-kalibrier-offset)
    - [3. RGB-LED-Befehl](#3-rgb-led-befehl)
    - [I²C-Statusabfrage](#ic-statusabfrage)
  - [Motorsteuerung und Kalibrierung](#motorsteuerung-und-kalibrierung)
    - [Bewegungsablauf](#bewegungsablauf)
    - [Referenzfahrt beim Start](#referenzfahrt-beim-start)
    - [Kalibrier-Offset](#kalibrier-offset)
    - [Schutz vor Überhitzung](#schutz-vor-überhitzung)
  - [RGB-LED-Steuerung](#rgb-led-steuerung)
  - [Sleep-Modus und Energieverbrauch](#sleep-modus-und-energieverbrauch)
  - [Statusrückmeldung an den Master](#statusrückmeldung-an-den-master)
  - [Firmware kompilieren und flashen](#firmware-kompilieren-und-flashen)
    - [Voraussetzungen](#voraussetzungen)
    - [Kompilieren](#kompilieren)
  - [Test- und Debug-Modi](#test--und-debug-modi)
    - [SERIAL\_ENABLE](#serial_enable)
    - [TEST\_ENABLE](#test_enable)
  - [Fehlerbehebung](#fehlerbehebung)
    - [Unit antwortet nicht auf I²C](#unit-antwortet-nicht-auf-ic)
    - [Motor bewegt sich nicht](#motor-bewegt-sich-nicht)
    - [Kalibrierung schlägt fehl](#kalibrierung-schlägt-fehl)
    - [LED leuchtet nicht](#led-leuchtet-nicht)
    - [Zeichenposition stimmt nicht](#zeichenposition-stimmt-nicht)
  - [Hinweise zum aktuellen Quellcode](#hinweise-zum-aktuellen-quellcode)

## Features

- Steuerung eines einzelnen Split-Flap-Trommelmotors.
- 45 Zeichenpositionen pro Umdrehung.
- 28BYJ-48-Schrittmotor mit 2038 Schritten pro Umdrehung (Firmware-Konstante).
- Automatische Referenzfahrt beim Einschalten.
- Hall-Sensor zur Erkennung der Referenzposition.
- Kalibrier-Offset in EEPROM, damit die Zeichenposition mechanisch justiert werden kann.
- Einstellbare I²C-Adresse über vier Hardware-Schalter (Adressen 0 bis 15).
- I²C-Kommandos für Zeichenposition, Geschwindigkeit und Kalibrier-Offset.
- Statusabfrage durch den Master.
- Eine RGB-LED pro Unit, angesteuert mit der Adafruit-NeoPixel-Bibliothek.
- RGB-Farbe und Helligkeit werden über I²C empfangen.
- Motor-Treiber wird nach einer Bewegung deaktiviert.
- Sleep-Modus bei Inaktivität.
- Schutz vor zu häufigen Motorbewegungen und wiederholten fehlgeschlagenen Kalibrierungen.
- Optionaler serieller Debug- und Testmodus.

## Hardware und Pinbelegung

UART Verbindung:    
RX Unit an TX USB-to-TTL   
TX Unit an RX USB-to-TTL   

Die Pinbelegung entspricht den `#define`-Werten im bereitgestellten Quellcode.

| Arduino Nano Pin | Funktion | Quellcode |
|---|---|---|
| D2 | NeoPixel-/WS2811B-Datenleitung | `LED_PIN` |
| D3 | I²C-Adressschalter 4 | `ADRESSSW4` |
| D4 | I²C-Adressschalter 3 | `ADRESSSW3` |
| D5 | I²C-Adressschalter 2 | `ADRESSSW2` |
| D6 | I²C-Adressschalter 1 | `ADRESSSW1` |
| D7 | Hall-Sensor | `HALLPIN` |
| D8 | Schrittmotor-Treiber Eingang 4 | `STEPPERPIN4` |
| D9 | Schrittmotor-Treiber Eingang 3 | `STEPPERPIN3` |
| D10 | Schrittmotor-Treiber Eingang 2 | `STEPPERPIN2` |
| D11 | Schrittmotor-Treiber Eingang 1 | `STEPPERPIN1` |
| A4 / SDA | I²C-Datenleitung | `Wire` |
| A5 / SCL | I²C-Taktleitung | `Wire` |

**Achtung:** Im Quellcode-Kommentar steht, D12 sei für die LED frei. Tatsächlich ist `LED_PIN` auf **D2** gesetzt. Maßgeblich ist die Definition `#define LED_PIN 2`.

### RGB-LED

Die Firmware initialisiert genau eine adressierbare RGB-LED:

```cpp
#define LED_PIN 2
#define LED_COUNT 1
Adafruit_NeoPixel statusLed(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
```

Die Datenleitung der LED wird an D2 angeschlossen. Die LED benötigt eine geeignete Versorgung und gemeinsame Masse mit dem Nano. Bei einer externen 5-V-Versorgung müssen die Massepotentiale verbunden sein. Die zulässigen Logikpegel und die konkrete LED-Bauform sind anhand der verwendeten Hardware zu prüfen.

### Schrittmotor

Der Code ist für einen 28BYJ-48-Schrittmotor mit passendem Treibermodul ausgelegt. Die vier Steuersignale werden über D8 bis D11 ausgegeben.

### Hall-Sensor

Der Hall-Sensor liegt an D7. Die Kalibrierlogik wertet `HIGH` als noch nicht erreichte Markierung und `LOW` als erkannte Referenzmarkierung aus. Der Sensor bzw. seine Beschaltung muss daher zu dieser Logik passen.

### I²C-Bus

Der Nano arbeitet als I²C-Slave. SDA liegt auf A4, SCL auf A5. Die I²C-Adresse wird beim Start aus den vier Adressschaltern gelesen und anschließend an `Wire.begin(i2cAddress)` übergeben.

## I²C-Adresse einstellen

Vier Eingänge mit `INPUT_PULLUP` lesen die Adressschalter ein. Die Berechnung lautet:

```cpp
address = !SW4 + (!SW3 * 2) + (!SW2 * 4) + (!SW1 * 8);
```

Die Schalter bilden somit eine 4-Bit-Adresse von 0 bis 15.

| Schalter | Wert |
|---|---:|
| SW4 (D3) | 1 |
| SW3 (D4) | 2 |
| SW2 (D5) | 4 |
| SW1 (D6) | 8 |

Durch `INPUT_PULLUP` gilt: Ein offener Schalter wird als HIGH gelesen und trägt 0 bei. Ein geschlossener Schalter gegen GND wird als LOW gelesen und trägt seinen jeweiligen Wert bei.

Beispiele:

| SW1 | SW2 | SW3 | SW4 | I²C-Adresse |
|---|---|---|---|---:|
| offen | offen | offen | offen | 0 |
| offen | offen | offen | geschlossen | 1 |
| offen | offen | geschlossen | offen | 2 |
| offen | geschlossen | offen | offen | 4 |
| geschlossen | offen | offen | offen | 8 |
| geschlossen | geschlossen | geschlossen | geschlossen | 15 |

Jede Unit am selben I²C-Bus muss eine eindeutige Adresse besitzen.

## Zeichensatz und Positionen

Die Trommel ist in der Firmware mit 45 Positionen definiert. Der Index 0 ist das Leerzeichen.

| Index | Zeichen | Index | Zeichen | Index | Zeichen |
|---:|:---:|---:|:---:|---:|:---:|
| 0 | Leerzeichen | 15 | O | 30 | 0 |
| 1 | A | 16 | P | 31 | 1 |
| 2 | B | 17 | Q | 32 | 2 |
| 3 | C | 18 | R | 33 | 3 |
| 4 | D | 19 | S | 34 | 4 |
| 5 | E | 20 | T | 35 | 5 |
| 6 | F | 21 | U | 36 | 6 |
| 7 | G | 22 | V | 37 | 7 |
| 8 | H | 23 | W | 38 | 8 |
| 9 | I | 24 | X | 39 | 9 |
| 10 | J | 25 | Y | 40 | : |
| 11 | K | 26 | Z | 41 | . |
| 12 | L | 27 | Ä | 42 | - |
| 13 | M | 28 | Ö | 43 | ? |
| 14 | N | 29 | Ü | 44 | ! |

Der Master sendet den Zeichenindex, nicht den ASCII-Code des Zeichens. Der Index muss zwischen 0 und 44 liegen.

Die Position wird mit ganzzahliger Rundung in Motorschritte umgerechnet:

```cpp
steps = (flap * STEPS + AMOUNTFLAPS / 2) / AMOUNTFLAPS;
```

Dadurch wird jede Zeichenposition aus einer absoluten Schrittposition berechnet und ein fortlaufender Rundungsfehler vermieden.

## I²C-Protokoll

Die Unit empfängt I²C-Daten über `receiveLetter()` und verarbeitet sie im Receive-Callback. Langsamere Aktionen wie LED-Ausgabe und EEPROM-Schreiben werden nicht direkt im Interrupt ausgeführt, sondern im normalen `loop()` nachgeholt.

### 1. Zeichen- und Motorbefehl

Zwei Bytes:

| Byte | Inhalt |
|---|---|
| 0 | Zeichenindex (0–44) |
| 1 | Motorgeschwindigkeit (RPM-Wert) |

Beispiel: Zeichenindex 1 (A) mit Geschwindigkeit 10:

```text
[0x01, 0x0A]
```

Die Geschwindigkeit wird auf den Bereich 1 bis 12 begrenzt. 0 wird auf 1 gesetzt; Werte über 12 werden auf 12 begrenzt.

### 2. Zeichenbefehl mit Kalibrier-Offset

Vier Bytes:

| Byte | Inhalt |
|---|---|
| 0 | Zeichenindex (0–44) |
| 1 | Motorgeschwindigkeit |
| 2 | Kalibrier-Offset, Low-Byte |
| 3 | Kalibrier-Offset, High-Byte |

Der Offset wird Little-Endian zusammengesetzt:

```cpp
offset = data[2] | (data[3] << 8);
```

Zulässig sind Werte von 0 bis `STEPS` (2038). Der neue Wert wird im Hauptprogramm in EEPROM gespeichert, nicht im I²C-Interrupt.

### 3. RGB-LED-Befehl

Fünf Bytes:

| Byte | Inhalt |
|---|---|
| 0 | `0xFF` – Kennung für LED-Kommando |
| 1 | Helligkeit (0–255) |
| 2 | Rot (0–255) |
| 3 | Grün (0–255) |
| 4 | Blau (0–255) |

Beispiel: LED mit voller Helligkeit grün:

```text
[0xFF, 0xFF, 0x00, 0xFF, 0x00]
```

Beispiel: LED aus:

```text
[0xFF, 0x00, 0x00, 0x00, 0x00]
```

Der LED-Befehl wird anhand des ersten Bytes `0xFF` erkannt. Nur ein Paket mit genau fünf Bytes löst eine LED-Aktualisierung aus.

### I²C-Statusabfrage

Wenn der Master die Unit per I²C anfragt, liefert `requestEvent()` ein Statusbyte zurück:

| Wert | Bedeutung |
|---:|---|
| 0 | Unit im Leerlauf |
| 1 | Motor bewegt sich oder eine Bewegung steht noch aus |
| 2 | Letzte Kalibrierung fehlgeschlagen |

Status 1 wird auch zurückgegeben, wenn ein neuer Zeichenbefehl empfangen wurde, die Bewegung aber wegen des Motorschutz-Zeitlimits noch nicht begonnen hat.

## Motorsteuerung und Kalibrierung

### Bewegungsablauf

1. Der Master sendet Zeichenindex und Geschwindigkeit.
2. Die Unit übernimmt den Zeichenindex und begrenzt die Geschwindigkeit auf 1 bis 12.
3. Die Hauptschleife erkennt die neue Zielposition.
4. Die Unit wartet gegebenenfalls auf das Ende der Schutzzeit seit der letzten Motorbewegung.
5. Der Motor wird aktiviert und zur Zielposition bewegt.
6. Nach der Bewegung wartet die Firmware 100 ms und schaltet die Motorwicklungen ab.
7. `displayedLetter` wird auf die neue Position gesetzt.

Die Drehrichtung wird über `ROTATIONDIRECTION` festgelegt. Im aktuellen Code ist der Wert `-1`.

### Referenzfahrt beim Start

Beim Einschalten wird `calibrate(true)` aufgerufen.

- Der Motor dreht, bis der Hall-Sensor die Referenzmarkierung erkennt.
- Wird die Markierung direkt beim Start erkannt, fährt der Motor zunächst 50 Schritte aus dem Erfassungsbereich heraus.
- Danach wird die Markierung erneut gesucht.
- Nach Erkennung der Markierung wird der gespeicherte Kalibrier-Offset angefahren.
- Die angezeigte Position wird auf Index 0 (Leerzeichen) gesetzt.
- Bei der initialen Kalibrierung wird der Motor anschließend abgeschaltet.

Die Kalibrierung bricht nach mehr als `3 * STEPS` Suchschritten ab. Das verhindert, dass der Motor bei einem defekten oder falsch angeschlossenen Sensor unbegrenzt weiterläuft.

### Kalibrier-Offset

Der Offset wird als `uint16_t` an EEPROM-Adresse 0 gespeichert. Beim Start wird er mit `EEPROM.get()` geladen. Ein ungültiger Wert größer als 2038 wird auf 0 zurückgesetzt.

Ein über I²C empfangener Offset wird zunächst vorgemerkt und anschließend in `loop()` geschrieben. Ein Schreibvorgang erfolgt nur, wenn sich der Wert tatsächlich geändert hat.

### Schutz vor Überhitzung

Nach Beginn einer Bewegung verhindert `OVERHEATINGTIMEOUT` weitere Bewegungen für zwei Sekunden. Dadurch wird verhindert, dass der Motor unmittelbar wiederholt angesteuert wird.

Nach fehlgeschlagener Kalibrierung wird eine erneute Referenzfahrt erst nach `CAL_RETRY_TIMEOUT` (60 Sekunden) versucht. Bis dahin werden neue Bewegungsanforderungen verworfen bzw. auf die zuletzt bekannte Anzeige zurückgesetzt.

## RGB-LED-Steuerung

Jede Unit besitzt laut Firmware eine adressierbare RGB-LED. Der Nano verwendet die Adafruit-NeoPixel-Bibliothek mit:

- Datenpin D2
- 1 Pixel
- Farbreihenfolge `NEO_GRB`
- Protokollgeschwindigkeit `NEO_KHZ800`

Nach dem Einschalten wird die LED initialisiert und ausgeschaltet. Die Farbe und Helligkeit werden anschließend über das 5-Byte-I²C-LED-Paket vom Master gesetzt.

Der I²C-Interrupt speichert nur die empfangenen Farbwerte und setzt `ledUpdatePending`. Im normalen `loop()` werden die Werte übernommen und mit `setBrightness()`, `setPixelColor()` und `show()` ausgegeben.

Der Startup-Test mit nacheinander grün aufleuchtenden Modulen wird vom Master koordiniert, indem er die LED-Kommandos an die einzelnen Units sendet. Die Unit-Firmware selbst startet keinen zeitgesteuerten grünen Blinktest.

## Sleep-Modus und Energieverbrauch

Nach mindestens zwei Sekunden Inaktivität kann die Unit in den Sleep-Modus wechseln.

Voraussetzungen:
- Die angezeigte Position entspricht der zuletzt empfangenen Zielposition.
- Es steht kein LED-Update aus.
- Es steht kein EEPROM-Update aus.
- Es liegt kein fehlgeschlagener Kalibrierstatus vor.

Ohne `SERIAL_ENABLE` wird `SLEEP_MODE_PWR_DOWN` verwendet. Mit `SERIAL_ENABLE` wird `SLEEP_MODE_IDLE` verwendet.

Vor dem Schlafen wird der ADC deaktiviert. Nach dem Aufwachen wird der vorherige ADC-Zustand wiederhergestellt und die TWI/I²C-Hardware erneut aktiviert.

Während eines fehlgeschlagenen Kalibrierstatus bleibt die Unit wach, damit die 60-Sekunden-Wartezeit für den nächsten Kalibrierversuch mit `millis()` ablaufen kann.

## Statusrückmeldung an den Master

Der Master kann den Status jederzeit über die I²C-Adresse der Unit abfragen.

| Status | Interpretation im Master |
|---:|---|
| 0 | Bewegung abgeschlossen / idle |
| 1 | beschäftigt oder Bewegung ausstehend |
| 2 | Kalibrierung fehlgeschlagen |

Der Fehlerstatus bleibt gesetzt, bis eine spätere Kalibrierung erfolgreich abgeschlossen wurde.

## Firmware kompilieren und flashen

### Voraussetzungen

- Arduino Nano mit ATmega328P.
- Arduino IDE oder PlatformIO.
- USB-Kabel bzw. USB-zu-Seriell-Verbindung.
- Installierte Bibliothek **Adafruit NeoPixel**.
- Die im Arduino-Core enthaltenen Bibliotheken `Wire`, `Stepper`, `EEPROM` und `avr/sleep.h`.

### Kompilieren

Die Datei als Arduino-Sketch öffnen. Falls die Datei nicht bereits in einem Sketch-Ordner liegt, muss sie entsprechend der Arduino-IDE-Konvention in einem Ordner mit gleichem Namen abgelegt werden.

In der Arduino IDE auswählen:
- Board: Arduino Nano
- Prozessor: passend zum verwendeten Nano (ATmega328P bzw. ATmega328P Old Bootloader, abhängig vom Board)
- Richtigen seriellen Port auswählen

Danach kompilieren und hochladen.

> Im bereitgestellten Material liegt die Unit-Firmware als `.ino`-Quelltext vor. Eine PlatformIO-Konfiguration für die Nano-Unit war nicht Teil der Datei. Ein konkreter PlatformIO-Build-Befehl kann deshalb aus diesem Quellstand nicht abgeleitet werden.

## Test- und Debug-Modi

Im Quellcode können zwei Compiler-Schalter aktiviert werden:

```cpp
//#define SERIAL_ENABLE
//#define TEST_ENABLE
```

### SERIAL_ENABLE

Wenn `SERIAL_ENABLE` aktiviert wird:

- Serielle Schnittstelle startet mit 115200 Baud.
- Startmeldungen und I²C-Adresse werden ausgegeben.
- Kalibrierung und Motorbewegungen werden protokolliert.
- Serielle Kommandos werden verarbeitet.
- Der Sleep-Modus wird auf `SLEEP_MODE_IDLE` umgestellt.

Serielle Kommandos:
- Ein einzelnes Zeichen gefolgt von Newline fordert die entsprechende Zeichenposition an.
- `test` startet eine Kalibrierung und anschließend den Testlauf.
- Eine Zahl setzt den Kalibrier-Offset und startet eine erneute Kalibrierung.

### TEST_ENABLE

Wenn `TEST_ENABLE` aktiviert wird, läuft nach der initialen Kalibrierung automatisch ein Test über zehn definierte Positionen. Die erwartete Reihenfolge ist laut Quellcode-Kommentar:

```text
" ", "Z", "A", "U", "N", "?", "0", "1", "2", "9"
```

Zwischen den Positionen liegt eine Pause von zwei Sekunden.

Für `SERIAL_ENABLE` oder `TEST_ENABLE` wird die Test-Zeichenliste `calLetters[]` eingebunden.

## Fehlerbehebung

### Unit antwortet nicht auf I²C

- Versorgung und gemeinsame Masse prüfen.
- SDA an A4 und SCL an A5 prüfen.
- I²C-Adresse über die vier Schalter kontrollieren.
- Sicherstellen, dass keine andere Unit dieselbe Adresse verwendet.
- Pull-up-Widerstände des I²C-Busses prüfen.
- Bei aktiviertem `SERIAL_ENABLE` die beim Start ausgegebene Adresse kontrollieren.

### Motor bewegt sich nicht

- Versorgung des Motors und Treibermoduls prüfen.
- D8 bis D11 und die Reihenfolge der Motoranschlüsse prüfen.
- Prüfen, ob die Unit noch innerhalb der zweisekündigen Schutzzeit liegt.
- I²C-Zeichenindex muss zwischen 0 und 44 liegen.
- Statusbyte des Masters prüfen.

### Kalibrierung schlägt fehl

- Hall-Sensor und Magnetposition kontrollieren.
- Prüfen, ob der Sensor bei Erreichen der Markierung LOW liefert.
- Sensorleitung an D7 prüfen.
- Nach einem Fehler wartet die Firmware 60 Sekunden bis zum nächsten automatischen Kalibrierversuch.
- Status 2 wird an den Master gemeldet.

### LED leuchtet nicht

- Datenleitung an D2 prüfen (nicht D12).
- Versorgung und gemeinsame Masse prüfen.
- LED-Typ und Datenprotokoll kontrollieren.
- Prüfen, ob der Master ein korrektes 5-Byte-Paket mit `0xFF` als erstem Byte sendet.
- Prüfen, ob die I²C-Adresse der Unit korrekt ist.

### Zeichenposition stimmt nicht

- Mechanische Ausrichtung und Hall-Referenzposition prüfen.
- Kalibrier-Offset kontrollieren.
- Prüfen, ob die Drehrichtung `ROTATIONDIRECTION` zur Mechanik passt.
- Sicherstellen, dass die Master- und Unit-Firmware dieselbe Zeichenindex-Tabelle verwenden.

## Hinweise zum aktuellen Quellcode

1. **Pin-Kommentar:** Der Kommentar nennt D12 als freien LED-Pin; tatsächlich verwendet die Firmware D2 (`LED_PIN 2`).
2. **Eine LED pro Unit:** `LED_COUNT` ist 1. Die Firmware steuert nicht zehn LEDs an einem Nano an, sondern eine LED pro Unit. Bei zehn Units gibt es damit insgesamt zehn einzeln adressierbare LEDs über den Master.
3. **Adressbereich:** Die Hardwareadressierung ermöglicht 0 bis 15. Der Master muss die Adresskonvention der Unit berücksichtigen.
4. **Zeichenkodierung:** Die Zeichenliste enthält `Ä`, `Ö` und `Ü` als `char`-Zeichenliterale. Auf AVR/GCC können solche Nicht-ASCII-Zeichen je nach Quelltextkodierung und Compiler zu Mehrzeichen-Konstanten bzw. unerwarteten Werten führen. Die I²C-Kommunikation über numerische Zeichenindizes ist davon nicht betroffen; die Zeichenliste sollte bei Bedarf mit expliziten Byte-/Glyphenwerten vereinheitlicht werden.
5. **Motorbewegung ist blockierend:** `Stepper.step()` und die Kalibrierschleife laufen synchron. Währenddessen kann die Unit keine weiteren Aufgaben in `loop()` bearbeiten. I²C-Empfang wird durch den Wire-Callback entgegengenommen, die Motorbewegung selbst wird aber erst nach Rückkehr in die Hauptschleife ausgeführt.
6. **Sleep/Wakeup:** Der Code schaltet die TWI-Hardware vor dem Sleep ab und initialisiert `Wire` nach dem Aufwachen erneut. Das Verhalten sollte mit der konkreten I²C-Master-Hardware im Dauerbetrieb getestet werden.
7. **I²C-Paketlänge:** Die LED-Verarbeitung akzeptiert genau fünf Bytes. Motorbefehle werden ab zwei Bytes verarbeitet; ein Kalibrier-Offset wird bei mindestens vier Bytes ausgewertet.
8. **Statusbyte:** Status 1 umfasst sowohl eine laufende Bewegung als auch eine ausstehende Bewegung. Der Master sollte nicht ausschließlich anhand eines einzelnen Statusabfrufs auf eine abgeschlossene Bewegung schließen, sondern den Status wiederholt abfragen.
9. **EEPROM:** Der Offset wird bei jeder tatsächlichen Änderung geschrieben. Häufige Offsetänderungen sollten vermieden werden, um die Lebensdauer des EEPROM nicht unnötig zu beanspruchen.

---

**Zusammenhang mit dem Master:** Der Master übernimmt WLAN, MQTT, Home-Assistant-Anbindung, Nachrichtenwarteschlange und Display-Animationen. Er übersetzt die gewünschte Anzeige in Zeichenindizes und sendet diese an die jeweilige Nano-Unit. LED-Farbwerte aus MQTT werden vom Master in das beschriebene I²C-LED-Paket umgewandelt.
