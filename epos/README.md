# Bontastic ePOS firmware

This is a stripped-down ESP32 firmware for direct Epson ePOS-style HTTP printing.
It does not use Meshtastic code.

The firmware is not a full Epson ePOS implementation. It implements the subset
needed by Odoo POS and maps that subset to a small ESC/POS thermal printer over
UART.

## Arduino dependencies

- ESP32 Arduino core
- NimBLE-Arduino

Open `epos/epos.ino` in Arduino IDE and flash it as an ESP32 sketch.

## Configure WiFi

Open `web/index.html` in a Web Bluetooth capable browser, connect to `Bontastic ePOS`, save SSID/password and printer pins, then press connect or reboot.

The ESP32 exposes:

- `POST http://<ip>/cgi-bin/epos/service.cgi?devid=local_printer&timeout=10000`
- `POST http://<ip>/print`
- `POST http://<ip>/`

Empty POSTs return a successful Epson-style status response for Odoo printer probes.

## Implemented ePOS subset

The HTTP endpoint accepts Epson ePOS-style SOAP requests:

```xml
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
  <s:Body>
    <epos-print xmlns="http://www.epson-pos.com/schemas/2011/03/epos-print">
      ...
    </epos-print>
  </s:Body>
</s:Envelope>
```

The XML parser is intentionally small:

- It searches for the `epos-print` root element, with or without an XML namespace prefix.
- It accepts raw `text/xml` or `text/plain` POST bodies.
- It also accepts Arduino `WebServer` form-style `plain` or `printdata` arguments as fallback.
- It handles large Odoo image payloads through the raw body callback, because normal argument parsing is not reliable for 50 KB+ receipts.
- Unknown ePOS tags are ignored.

The response is always Epson-shaped XML:

```xml
<response xmlns="http://www.epson-pos.com/schemas/2011/03/epos-print" success="true" code="" status="0" battery="0"/>
```

For image prints the firmware replies before the actual thermal print finishes.
This avoids Odoo timing out while the ESP32 decodes and streams the raster data.

## Supported XML tags

### `text`

Prints text content and decodes the small entity subset Odoo uses:

- `&lt;`
- `&gt;`
- `&quot;`
- `&apos;`
- `&amp;`
- `&#10;`
- `&#x0a;`

Supported attributes:

- `align="left|center|right"` mapped to `ESC a`
- `font="font_a|font_b"` mapped to `ESC M`
- `width="1..8"` and `height="1..8"` mapped to `GS !`
- `dw="true"` and `dh="true"` as shortcuts for double width/height
- `bold="true"` mapped to `ESC E`
- `reverse="true"` mapped to `GS B`
- `ul="true"` or `underline="true"` mapped to `ESC -`
- `linespc="0..255"` mapped to `ESC 3`

### `feed`

Supports:

- `line="n"` mapped to `ESC d n`

### `cut`

The target printer has no real cutter. The firmware maps this to a feed of
three lines.

### `image`

Supports Odoo's raster image receipts:

```xml
<image width="512" height="650" align="center">base64...</image>
```

Implemented behavior:

- The body must be base64 encoded 1-bit packed raster data.
- Width and height are read from the tag.
- Images wider than the printer are scaled to `384` dots.
- Height is scaled by the same ratio, so QR codes stay square.
- The image is decoded one source row at a time and printed in 24-row raster bands.
- Each band is sent with `GS v 0`.
- `align` is applied before printing, so Odoo's `align="center"` works.

Example status after a successful Odoo image receipt:

```text
code=IMG_OK_ASPECT_384x488_SRC_512x650
build=epos-img-aspect-v5
```

### `barcode`

Prints simple ESC/POS barcodes.

Supported attributes:

- `type` with basic handling for `ean13` and `code39`
- other values fall back to Code 128 mode

Mapped commands:

- `GS h`
- `GS w`
- `GS H`
- `GS k`

### `qrcode` / `symbol`

Prints QR codes through ESC/POS `GS ( k`.

Supported attributes:

- `size="1..16"`

The current implementation uses model 2 and default error correction.

## Printer setup commands

Printer settings configured through BLE are applied directly to the thermal
printer:

- heat dots/time/interval via `ESC 7`
- density and break time via `DC2 #`
- line height via `ESC 3`
- charset via `ESC R`
- code page via `ESC t`
- font via `ESC M`
- size via `GS !`
- justification via `ESC a`

The UART printer runs on `HardwareSerial(2)` at `9600 8N1`, so USB serial logs
are not tied to the printer stream.

## Not implemented

This firmware does not implement the full Epson ePOS XML schema. These parts
are intentionally out of scope for the current Odoo receipt flow:

- printer discovery
- device registration
- Epson status polling semantics
- real cutter control
- cash drawer control
- layout/page mode
- downloaded images or NV bitmaps
- advanced barcode options
- advanced QR options
- multiple printer devices under different `devid` values
- full XML parsing or validation

The supported device id used by Odoo is:

```text
local_printer
```

The firmware currently does not enforce the `devid` query parameter.

## Debug status

Open:

```text
http://<ip>/status
```

The status endpoint returns the last print code, configured RX/TX pins, IP,
firmware build marker, and a short description of the last HTTP request.

## Test request

```sh
curl -i \
  -H 'Content-Type: text/xml; charset=utf-8' \
  -H 'SOAPAction: ""' \
  --data-binary @samples/odoo-text.xml \
  'http://<ip>/cgi-bin/epos/service.cgi?devid=local_printer&timeout=10000'
```

The response should contain:

```xml
<response xmlns="http://www.epson-pos.com/schemas/2011/03/epos-print" success="true" code="" status="0" battery="0"/>
```
