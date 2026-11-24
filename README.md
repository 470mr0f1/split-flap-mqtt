# Split-Flap MQTT Display

Wi-Fi- und MQTT-gesteuerter Controller für ein Split-Flap-Display mit bis zu 10 Modulen. Die Firmware läuft laut aktuellem Quellcode auf einem **ESP8266** und steuert die Module über I²C.

Dieses Dokument beschreibt den Funktionsumfang, die MQTT-Schnittstelle, die Einrichtung sowie Beispiele für die Anbindung an **Home Assistant**.

> **Hinweis zum LED-Hardwarestand:** Der aktuelle Quellcode enthält RGB-LED-Kommandos, die über I²C an die Split-Flap-Module gesendet werden (`writeLedToUnit()`). Eine direkte Ansteuerung eines WS2811B-LED-Streifens an einem ESP8266-GPIO ist in den bereitgestellten Dateien nicht enthalten. Die LED-Beispiele in dieser README beziehen sich daher auf die im Firmware-Code vorhandene I²C-LED-Steuerung.

## Inhaltsverzeichnis

- [Features](#features)
- [Hardware und Architektur](#hardware-und-architektur)
- [Installation und Inbetriebnahme](#installation-und-inbetriebnahme)
- [WLAN- und MQTT-Konfiguration](#wlan--und-mqtt-konfiguration)
- [MQTT-Schnittstelle](#mqtt-schnittstelle)
- [Display-Nachrichten und Animationen](#display-nachrichten-und-animationen)
- [LED-Steuerung](#led-steuerung)
- [Home Assistant](#home-assistant)
- [Status, Health und Fehlerdiagnose](#status-health-und-fehlerdiagnose)
- [Webinterface und OTA-Updates](#webinterface-und-ota-updates)
- [Projektstruktur](#projektstruktur)
- [Bekannte Hinweise zum aktuellen Quellcode](#bekannte-hinweise-zum-aktuellen-quellcode)
- [Troubleshooting](#troubleshooting)
- [Credits und Lizenz](#credits-und-lizenz)

## Features

### Display-Steuerung

- Steuerung von bis zu 10 Split-Flap-Modulen.
- Nachrichten per MQTT an ein bestimmtes Display oder per Broadcast an alle Displays senden.
- Nachrichten können als Klartext oder als JSON mit zusätzlichen Parametern gesendet werden.
- Lange Texte werden in displaybreite Abschnitte aufgeteilt. Die Aufteilung erfolgt möglichst an Wortgrenzen; Wörter, die länger als die Displaybreite sind, werden in Teilstücke zerlegt.
- Jeder Nachrichtenabschnitt wird zentriert.
- Nachrichtenwarteschlange mit bis zu 10 Einträgen.
- Konfigurierbare Animationsgeschwindigkeit und Pause zwischen Nachrichten.

### Verfügbare Animationen

| MQTT-Wert | Beschreibung |
|---|---|
| `instant` | Alle Module erhalten die Zielposition direkt nacheinander über I²C. |
| `wave` | Die Zeichen werden mit zeitlichem Versatz von links nach rechts angesteuert. |
| `scroll` | Der Text wird schrittweise von rechts in das Display eingeschoben. |
| `random` | Die Zeichen werden in zufälliger Reihenfolge aufgedeckt. |
| `slot_machine` | Zufällige Zeichenbewegungen, danach wird der Zieltext eingestellt. |
| `scramble` | Alle Module werden mehrfach zufällig verstellt; anschließend wird der Text in zufälliger Reihenfolge aufgedeckt. |
| `center_out` | Die Zeichen werden von der Mitte nach außen eingestellt. |
| `typewriter` | Die Zeichen werden nacheinander von links nach rechts eingestellt. |
| `matrix` | Zufällige Zeichenwechsel, anschließend Auflösung des Zieltextes von links nach rechts. |
| `countdown` | Zufällige Zeichenwechsel mit zunehmenden Pausen, danach Ausgabe des Zieltextes. |
| `cascade` | Jedes Modul durchläuft mehrere zufällige Positionen und stellt anschließend sein Zielzeichen ein. |
| `spin` | Alle Module durchlaufen mehrere Positionen und stoppen anschließend auf dem Zieltext. |
| `glitch` | Zufällige Module springen mehrfach auf zufällige Zeichen; anschließend wird der Zieltext gesetzt. |
| `wipe` | Das Display wird zunächst geleert und danach von links nach rechts beschrieben. |

Die Animationsnamen werden ohne Beachtung der Groß-/Kleinschreibung ausgewertet. Unbekannte Namen fallen auf `instant` zurück. Der Alias `slot` wird ebenfalls als `slot_machine`, `center` als `center_out` interpretiert.

### RGB-LED-Funktionen

Der aktuelle Code kann RGB-Farbe und Helligkeit eines einzelnen Split-Flap-Moduls oder aller Module setzen. Die Werte werden als I²C-Kommando an die jeweilige Unit übertragen.

- RGB-Kanäle von 0 bis 255.
- Helligkeit von 0 bis 255.
- Einzelne Unit oder alle Units ansteuern.
- LEDs einzeln bzw. gemeinsam ausschalten.
- Beim Start wird ein LED-Test ausgeführt, der die Module nacheinander grün aufleuchten lässt.

**Wichtig:** Die LED-Kommandos werden vom ESP8266 über I²C gesendet. Ob und wie die LED-Hardware diese Kommandos umsetzt, hängt von der Firmware der jeweiligen Split-Flap-Unit ab. Eine WS2811B-Strip-Ansteuerung direkt am ESP8266 ist nicht Bestandteil dieses Quellstands.

### Netzwerk und Wartung

- WLAN-Einrichtung über WiFiManager-Captive-Portal.
- MQTT-Broker, Port, Benutzer, Passwort und Gerätename konfigurierbar.
- MQTT-Reconnect mit zeitlichem Abstand zwischen Verbindungsversuchen.
- MQTT-Last-Will-Nachricht (`offline`) und retained Online-Status.
- Geräte-ID aus der MAC-Adresse.
- Webinterface für Konfiguration und Geräteinformationen.
- OTA-Firmware-Updates über WLAN.
- Health-Meldungen mit Betriebs- und Fehlerkennzahlen.

## Hardware und Architektur

| Bestandteil | Implementierung im Quellcode |
|---|---|
| Controller | ESP8266 (im README-Vorgänger als ESP-01 bezeichnet) |
| Split-Flap-Kommunikation | I²C |
| I²C-Pins | `Wire.begin(4, 5)` – GPIO 4 als SDA, GPIO 5 als SCL |
| Anzahl Module | `MAX_NUMBER_UNITS = 10` |
| WLAN | ESP8266WiFi und WiFiManager |
| MQTT | PubSubClient |
| JSON | ArduinoJson |
| Konfigurationsspeicher | LittleFS |
| Webserver | ESP8266WebServer, Port 80 |
| OTA | ArduinoOTA |

Die I²C-Kommandos für die Motorsteuerung bestehen aus Zeichenposition und Geschwindigkeit. LED-Kommandos verwenden ein eigenes Kennbyte:

| Byte | Bedeutung |
|---|---|
| `0xFF` | LED-Kommando |
| 2 | Helligkeit (0–255) |
| 3 | Rot (0–255) |
| 4 | Grün (0–255) |
| 5 | Blau (0–255) |

Die tatsächliche Interpretation dieser Bytes muss zur Firmware der Split-Flap-Units passen.

## Installation und Inbetriebnahme

### Voraussetzungen

- PlatformIO (z. B. als VS-Code-Erweiterung).
- ESP8266-Board und passender USB-zu-Seriell-Programmieradapter.
- WLAN-Netzwerk.
- MQTT-Broker, beispielsweise Mosquitto.
- Split-Flap-Module am I²C-Bus.

### Firmware kompilieren und flashen

Im Projektverzeichnis:

```bash
pio run
```

Firmware per USB flashen:

```bash
pio run -t upload
```

Der konkrete PlatformIO-Environment-Name und die Upload-Ports werden in der nicht im ZIP enthaltenen `platformio.ini` festgelegt.

### Erstkonfiguration über WiFiManager

1. Controller einschalten.
2. Mit dem WLAN `Flaps-Setup-XXXXXX` verbinden.
3. Das Passwort lautet `flapsetup`.
4. Im Captive Portal das eigene WLAN auswählen und das WLAN-Passwort eingeben.
5. MQTT-Server, MQTT-Port, optional Benutzer und Passwort sowie den Gerätenamen eintragen.
6. Konfiguration speichern.

Der Controller verbindet sich anschließend mit dem WLAN und dem konfigurierten MQTT-Broker.

Wenn keine gültige Konfiguration vorhanden ist oder kein MQTT-Server gespeichert wurde, setzt der Code die gespeicherten WLAN-Einstellungen zurück und startet die Einrichtung erneut. Das Captive Portal hat ein Zeitlimit von 180 Sekunden.

## WLAN- und MQTT-Konfiguration

Die Konfiguration wird in LittleFS unter `/config.json` gespeichert. Folgende Werte werden verwendet:

| Parameter | Standardwert / Bedeutung |
|---|---|
| MQTT Server | Kein voreingestellter Server; muss konfiguriert werden |
| MQTT Port | `1883` |
| MQTT Benutzer | Optional |
| MQTT Passwort | Optional |
| Device Name | `flaps-01` |

Der Gerätename wird unter anderem als OTA-Hostname verwendet. Für die MQTT-Client-ID wird dagegen die MAC-basierte Geräte-ID benutzt, damit mehrere Geräte mit gleichem Gerätenamen nicht dieselbe MQTT-Client-ID verwenden.

## MQTT-Schnittstelle

### Topic-Übersicht

Die Geräte-ID besteht aus der MAC-Adresse ohne Trennzeichen, beispielsweise `68C63AF65362`.

| Topic | Richtung | Beschreibung |
|---|---|---|
| `/flaps/{device_id}/command` | Home Assistant → Gerät | Gerätespezifische Befehle |
| `/flaps/all/command` | Home Assistant → alle Geräte | Broadcast-Befehle |
| `/game` | Home Assistant → Gerät | Legacy-Topic für Klartextnachrichten |
| `/flaps/{device_id}/status` | Gerät → Broker | `online` bzw. Last Will `offline` |
| `/flaps/{device_id}/health` | Gerät → Broker | Health-JSON |
| `/flaps/registration` | Gerät → Broker | Registrierungs-/Check-in-Informationen |

Das Gerät abonniert beim MQTT-Verbindungsaufbau sein individuelles Command-Topic, das Broadcast-Topic und `/game`.

### Display-Nachricht als Klartext

```bash
mosquitto_pub -h BROKER_IP \
  -t "/flaps/DEVICE_ID/command" \
  -m "HELLO WORLD"
```

Klartextnachrichten verwenden die Standardanimation `instant`, die Standardgeschwindigkeit und eine Standardpause von 1000 ms nach Abschluss der Nachricht.

### Display-Nachricht als JSON

```bash
mosquitto_pub -h BROKER_IP \
  -t "/flaps/DEVICE_ID/command" \
  -m '{"text":"HELLO WORLD","animation":"wave","speed":10,"delay":1000}'
```

| JSON-Feld | Typ | Bedeutung |
|---|---|---|
| `text` | String | Anzuzeigender Text |
| `animation` | String | Animation aus der Animationstabelle |
| `speed` | Zahl | An das Unit-Kommando übergebene Geschwindigkeit |
| `delay` | Zahl | Pause nach Abschluss eines Nachrichtenabschnitts in Millisekunden; maximal 60000 ms |

Nicht angegebene Felder verwenden die Firmware-Standardwerte. `delay` wird auf maximal 60000 ms begrenzt.

### Broadcast an alle Displays

```bash
mosquitto_pub -h BROKER_IP \
  -t "/flaps/all/command" \
  -m '{"text":"WELCOME","animation":"center_out"}'
```

### Display löschen

```bash
mosquitto_pub -h BROKER_IP \
  -t "/flaps/DEVICE_ID/command" \
  -m '{"command":"clear","animation":"wipe","speed":10}'
```

Der `clear`-Befehl stellt eine Nachricht aus Leerzeichen in die Warteschlange. Die Animation kann angegeben werden.

## LED-Steuerung

Die LED-Befehle werden als JSON auf dem gerätespezifischen Command-Topic oder auf dem Broadcast-Topic gesendet.

### Alle Modul-LEDs auf eine Farbe setzen

```bash
mosquitto_pub -h BROKER_IP \
  -t "/flaps/DEVICE_ID/command" \
  -m '{"command":"led_all","brightness":128,"red":255,"green":0,"blue":0}'
```

Beispiel: Alle LEDs rot mit Helligkeit 128.

### Alle Modul-LEDs ausschalten

```bash
mosquitto_pub -h BROKER_IP \
  -t "/flaps/DEVICE_ID/command" \
  -m '{"command":"led_off_all"}'
```

### LED eines einzelnen Moduls setzen

```bash
mosquitto_pub -h BROKER_IP \
  -t "/flaps/DEVICE_ID/command" \
  -m '{"command":"led","address":1,"brightness":255,"red":0,"green":255,"blue":0}'
```

### LED eines einzelnen Moduls ausschalten

```bash
mosquitto_pub -h BROKER_IP \
  -t "/flaps/DEVICE_ID/command" \
  -m '{"command":"led_off","address":1}'
```

### LED-JSON-Felder

| Feld | Bedeutung |
|---|---|
| `command` | `led`, `led_off`, `led_all` oder `led_off_all` |
| `address` | Adresse des anzusteuernden Moduls beim Einzelkommando |
| `brightness` | Helligkeit 0–255 |
| `red` | Rotanteil 0–255 |
| `green` | Grünanteil 0–255 |
| `blue` | Blauanteil 0–255 |

Bei `led_off` und `led_off_all` werden Helligkeit und RGB-Werte auf 0 gesetzt. Bei Farbkommandos sind nicht angegebene RGB-Werte standardmäßig 0 und die nicht angegebene Helligkeit standardmäßig 255.

> **Adresshinweis:** Im aktuellen Quellcode gibt es eine Unstimmigkeit: Der Startup-Test und die Schleife für `led_all` adressieren die Units mit `0` bis `number_units - 1` (also 0–9). Die Validierung des Einzelkommandos akzeptiert dagegen `1` bis `number_units` (also 1–10) und übergibt den Wert unverändert an I²C. Vor der produktiven Nutzung der Einzeladressierung sollte diese Abweichung geprüft und vereinheitlicht werden. Die Beispiele oben zeigen die derzeitige Prüfung und verwenden daher `address: 1` als Beispiel, nicht als bestätigte Zuordnung zur ersten physischen Unit.

## Home Assistant

### 1. MQTT-Integration einrichten

1. In Home Assistant **Einstellungen → Geräte & Dienste → Integration hinzufügen** öffnen.
2. **MQTT** auswählen.
3. Broker-IP, Port (typischerweise `1883`) und gegebenenfalls Benutzer/Passwort eintragen.
4. Prüfen, ob Home Assistant mit demselben MQTT-Broker verbunden ist wie der Split-Flap-Controller.

Die folgenden Beispiele verwenden die Home-Assistant-Aktion `mqtt.publish`.

### 2. Skript zum Senden einer Nachricht

In `configuration.yaml` oder einer eingebundenen Script-Datei:

```yaml
script:
  splitflap_hello:
    alias: Split-Flap - Hello anzeigen
    sequence:
      - action: mqtt.publish
        data:
          topic: "/flaps/DEVICE_ID/command"
          payload: >
            {"text":"HELLO","animation":"wave","speed":10,"delay":1000}
          qos: 0
          retain: false
```

`DEVICE_ID` durch die MAC-basierte ID des Controllers ersetzen.

### 3. Skript zum Ändern der LED-Farbe aller Module

```yaml
script:
  splitflap_led_red:
    alias: Split-Flap - LEDs rot
    sequence:
      - action: mqtt.publish
        data:
          topic: "/flaps/DEVICE_ID/command"
          payload: >
            {"command":"led_all","brightness":128,"red":255,"green":0,"blue":0}
          qos: 0
          retain: false
```

### 4. LEDs ausschalten

```yaml
script:
  splitflap_led_off:
    alias: Split-Flap - LEDs ausschalten
    sequence:
      - action: mqtt.publish
        data:
          topic: "/flaps/DEVICE_ID/command"
          payload: >
            {"command":"led_off_all"}
          qos: 0
          retain: false
```

### 5. LED-Farbe dynamisch über Home Assistant auswählen

Das folgende Beispiel verwendet eine Home-Assistant-Farb- und Helligkeitsauswahl als Helfer. Die ausgewählten Werte werden beim Aufruf des Skripts an MQTT übertragen.

Zunächst Helfer anlegen (über **Einstellungen → Geräte & Dienste → Helfer**):
- `input_text.splitflap_led_color`: RGB-Hex-Farbe, z. B. `FF8000`.
- `input_number.splitflap_led_brightness`: Minimum 0, Maximum 255, Schritt 1.

Skript:

```yaml
script:
  splitflap_led_set:
    alias: Split-Flap - LED-Farbe setzen
    sequence:
      - variables:
          color: "{{ states('input_text.splitflap_led_color') | upper }}"
          brightness: "{{ states('input_number.splitflap_led_brightness') | int(128) }}"
          red: "{{ (color[0:2] | int(base=16)) if color | length == 6 else 0 }}"
          green: "{{ (color[2:4] | int(base=16)) if color | length == 6 else 0 }}"
          blue: "{{ (color[4:6] | int(base=16)) if color | length == 6 else 0 }}"
      - action: mqtt.publish
        data:
          topic: "/flaps/DEVICE_ID/command"
          payload: >
            {"command":"led_all","brightness":{{ brightness }},"red":{{ red }},"green":{{ green }},"blue":{{ blue }}}
          qos: 0
          retain: false
```

Das Skript erwartet einen sechsstelligen Hex-Farbwert ohne `#`, zum Beispiel `FF8000` für Orange. Die Werte werden in Rot, Grün und Blau umgerechnet.

### 6. Automation: Display bei einem Ereignis beschreiben

Beispiel: Eine Automation sendet bei Sonnenuntergang eine Nachricht.

```yaml
automation:
  - alias: Split-Flap - Sonnenuntergang
    trigger:
      - platform: sun
        event: sunset
    action:
      - action: mqtt.publish
        data:
          topic: "/flaps/DEVICE_ID/command"
          payload: >
            {"text":"GOOD EVENING","animation":"center_out","speed":10,"delay":1000}
          qos: 0
          retain: false
```

### 7. MQTT-Nachrichten in Home Assistant testen

Unter **Entwicklerwerkzeuge → Aktionen** die Aktion `mqtt.publish` auswählen und beispielsweise folgende Daten senden:

```yaml
action: mqtt.publish
data:
  topic: "/flaps/DEVICE_ID/command"
  payload: '{"command":"led_all","brightness":128,"red":0,"green":0,"blue":255}'
  qos: 0
  retain: false
```

Damit werden alle Modul-LEDs mit mittlerer Helligkeit auf Blau gesetzt.

**Wichtig:** Command-Nachrichten sollten normalerweise nicht retained gesendet werden. Sonst könnte der Broker einen alten Befehl nach einem Neustart erneut zustellen.

## Status, Health und Fehlerdiagnose

### Online-/Offline-Status

Beim erfolgreichen MQTT-Verbindungsaufbau veröffentlicht das Gerät retained:

- Topic: `/flaps/{device_id}/status`
- Payload: `online`

Als Last Will ist `offline` auf demselben Topic konfiguriert. Dadurch kann der Broker bei einem unerwarteten Verbindungsverlust den Offline-Status veröffentlichen.

### Health-Topic

Das Gerät veröffentlicht etwa alle 30 Sekunden oder unmittelbar bei einer Änderung des Kalibrierfehlerstatus ein JSON auf:

`/flaps/{device_id}/health`

Enthaltene Felder:

| Feld | Bedeutung |
|---|---|
| `uptime` | Laufzeit in Millisekunden |
| `messages_processed` | Anzahl der in die Warteschlange aufgenommenen Textabschnitte |
| `mqtt_disconnects` | Zähler der vom Reconnect-Code gezählten Verbindungsversuche bei nicht verbundenem MQTT-Client |
| `i2c_errors` | Summe der erfassten I²C-Fehler |
| `calibration_faults` | Anzahl erkannter Übergänge in einen Kalibrierfehler |
| `faulty_units` | Array der aktuell fehlerhaften Modulpositionen |
| `queue_depth` | Aktuelle Anzahl wartender Nachrichten |
| `wifi_rssi` | WLAN-Signalstärke in dBm |
| `free_heap` | Freier Heap-Speicher in Bytes |

### Registrierung / Check-in

Etwa einmal pro Minute veröffentlicht das Gerät auf `/flaps/registration` Informationen wie:

- Gerätename und Geräte-ID
- IP-Adresse
- Verbindungsstatus
- Uptime
- Firmware-Version
- zuletzt angezeigter Text
- Warteschlangentiefe

## Webinterface und OTA-Updates

### Webinterface

Nach erfolgreicher WLAN-Verbindung ist das Webinterface unter folgender Adresse erreichbar:

```text
http://DEVICE_IP/
```

Es zeigt Geräte-ID, IP-Adresse, MQTT-Server, Gerätenamen und Firmware-Version an. Außerdem können MQTT-Server, Port, Benutzer, Passwort und Gerätename geändert werden.

Weitere Aktionen:
- WLAN-Konfiguration zurücksetzen.
- Gerät neu starten.

Die im Webinterface angezeigten MQTT-Topics helfen dabei, die gerätespezifischen Topics zu ermitteln.

### OTA-Update

Die Firmware unterstützt ArduinoOTA. Der OTA-Hostname entspricht dem konfigurierten Gerätenamen.

Das im Quellcode hinterlegte OTA-Passwort lautet derzeit:

```text
flaps-ota-secret
```

**Sicherheit:** Dieses Passwort sollte vor einem produktiven Einsatz geändert werden. Das OTA-Passwort wird in `src/03_ota.ino` festgelegt.

Der genaue PlatformIO-Befehl hängt vom OTA-Environment in `platformio.ini` ab. Diese Datei war nicht Teil des bereitgestellten ZIP-Archivs.

## Projektstruktur

```text
src/
├── 00_main.ino          # setup(), loop(), Initialisierung und Hauptschleife
├── 01_config.ino        # Firmware-Konstanten, Geräte-ID und MQTT-Topics
├── 02_wifi.ino          # WiFiManager, WLAN und LittleFS-Konfiguration
├── 03_ota.ino           # OTA-Update-Funktion
├── 04_mqtt.ino          # MQTT-Client, JSON-Befehle, Queue und Health
├── 05_flaps.ino         # I²C-Kommunikation, Display und Animationen
├── 06_webconfig.ino     # Webinterface
├── 07_faults.ino        # Erkennung und Auswertung von Modulfehlern
└── declarations.h       # Gemeinsame Definitionen und Funktionsdeklarationen

README.md                # Projektdokumentation
```

## Bekannte Hinweise zum aktuellen Quellcode

Die folgenden Punkte sind bei der Verwendung bzw. Weiterentwicklung zu beachten:

1. **LED-Adressierung:** Die Einzel-LED-Validierung in `04_mqtt.ino` akzeptiert 1 bis 10, während andere LED-Schleifen 0 bis 9 verwenden. Die Adressierung sollte vereinheitlicht werden.
2. **Startup-LED-Test:** `updateStartupLedTest()` prüft aktuell `startupLedTestUnit > number_units`. Bei 10 Modulen kann dadurch zusätzlich die Adresse 10 angesprochen werden. Für nullbasierte Adressen wäre eine Prüfung `>= number_units` erforderlich.
3. **WS2811B:** Eine direkte WS2811B-LED-Strip-Ansteuerung durch den ESP8266 ist im vorliegenden Quellcode nicht vorhanden. Vorhanden ist die I²C-Weitergabe von RGB- und Helligkeitswerten an die Units.
4. **Animationen und MQTT-Verarbeitung:** Die Animationen enthalten teilweise längere Wartezeiten und blockierende Schleifen. Währenddessen kann die Bearbeitung von MQTT, Webserver und OTA verzögert sein.
5. **I²C-Fehlerbehandlung:** `writeToUnit()` und `writeLedToUnit()` prüfen den Rückgabewert von `Wire.endTransmission()` derzeit nicht. Die Health-Metrik `i2c_errors` erfasst vor allem Fehler beim Lesen/Abfragen des Modulstatus.
6. **MQTT-Nachrichtenlänge:** Der MQTT-Puffer ist auf 512 Bytes gesetzt. Sehr große Nachrichten können trotzdem nicht verarbeitet werden.
7. **OTA-Sicherheit:** Das im Quellcode hinterlegte Standardpasswort sollte geändert werden.

## Troubleshooting

### Setup-WLAN wird nicht angezeigt

- Prüfen, ob das Gerät bereits mit einem WLAN verbunden ist.
- Gerät neu starten.
- Bei Bedarf die WLAN-Konfiguration über das Webinterface zurücksetzen.
- Setup-Netzwerk: `Flaps-Setup-XXXXXX`, Passwort `flapsetup`.

### MQTT-Verbindung funktioniert nicht

- Broker-IP und Port kontrollieren.
- Prüfen, ob Benutzername und Passwort korrekt sind.
- Prüfen, ob Home Assistant und Display denselben Broker verwenden.
- Gerätespezifische ID und Topic kontrollieren.
- MQTT-Status auf `/flaps/{device_id}/status` prüfen.

### Nachricht wird nicht angezeigt

- Prüfen, ob das Gerät online ist.
- Topic und JSON-Syntax kontrollieren.
- Prüfen, ob die Warteschlange voll ist.
- Nur Zeichen verwenden, die in der `letters[]`-Tabelle in `01_config.ino` enthalten sind. Nicht unterstützte Zeichen werden auf Leerzeichen abgebildet.
- Health-Topic und serielle Ausgabe prüfen.

### LED reagiert nicht

- Prüfen, ob die Unit-Firmware das I²C-LED-Kommando `0xFF` unterstützt.
- I²C-Verkabelung und Unit-Adresse kontrollieren.
- Bei Einzelkommandos die beschriebene Adressierungsabweichung beachten.
- Prüfen, ob Helligkeit und RGB-Werte im Bereich 0–255 liegen.

### OTA-Update schlägt fehl

- Prüfen, ob sich Rechner und Display im selben Netzwerk befinden.
- OTA-Hostname und Passwort prüfen.
- Gerät muss eingeschaltet und im WLAN verbunden sein.
- PlatformIO-OTA-Environment und Upload-Adresse kontrollieren.

## Credits und Lizenz

- Ursprüngliches Hardware-/Split-Flap-Projekt: [Dave19171/split-flap](https://github.com/Dave19171/split-flap)
- Printables: [Split-Flap Display](https://www.printables.com/model/69464-split-flap-display)
