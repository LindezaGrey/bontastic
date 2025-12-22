<p align="center">
	<img src="Bontastic/web/assets/logo.png" alt="Bontastic" width="420" />
</p>

<h1 align="center">Bontastic</h1>

<p align="center">From mesh to paper — Meshtastic with Proof of Print (PoP).</p>

## Why

Meshtastic has major problems: It is only digital, it is not blockchain compatible and if you want to check for new messages you need a compatible device.

We tackled those problems and created a device that connects to the meshtastic network and prints every message it receives on thermal-paper to preserve it. That way, we have a save, long-term storage for those messages (do not put it in direct sunlight).

Bontastic adds the PoP (Proof of Print) to meshtastic and makes it fully blockchain compatible, each owner of a printer has a ledger of printed messages

In emergency situations it can help to provide real time feedback about what is going on in the mesh without the need for a mobile phone.

## What it is

Bontastic is a small ESP32-based “mesh-to-thermal” gateway:

- Connects to the Meshtastic network
- Receives messages
- Prints them on a thermal printer for an always-on physical log

## What’s in this repo

- **Firmware** (Arduino/ESP32): main sketch and printer/BLE code
- **Thermal driver**: a lightweight ESC/POS-style driver used by the firmware
- **Web UI**: a static page that can talk to the device over Web Bluetooth (for printer control / POC)

## Project layout

- Firmware entry point: [Bontastic/Bontastic.ino](Bontastic/Bontastic.ino)
- Printer code: [Bontastic/src/printer/](Bontastic/src/printer/)
- Web UI: [Bontastic/web/index.html](Bontastic/web/index.html)

## Dependencies

- ESP32 Arduino core (Arduino IDE / CLI)
- NimBLE-Arduino: https://github.com/h2zero/NimBLE-Arduino

## Build & flash (high level)

1. Install the ESP32 board support for Arduino.
2. Install NimBLE-Arduino.
3. Open [Bontastic/Bontastic.ino](Bontastic/Bontastic.ino), select your ESP32 board/port, upload.

## License

See [LICENSE](LICENSE).