/*
  AtomS3-Lite: BLE GATT (Web Bluetooth) -> USB HID (Keyboard + Mouse) + Serial debug
  - Keyboard: write UTF-8 to kbd characteristic (prints + '\n' optional on frontend)
  - Mouse: 4-byte packet: [buttons, dx, dy, wheel] (int8 dx/dy/wheel)
    buttons bitmask: bit0=Left, bit1=Right, bit2=Middle
*/

#include <M5Unified.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>
#include <USBHIDConsumerControl.h>

// RGB LED (AtomS3 Lite has one WS2812/SK6812 LED). Using Adafruit_NeoPixel for reliability across cores.
// AtomS3 Lite LED data pin is GPIO35; adjust if your board revision differs.
#include <Adafruit_NeoPixel.h>
#ifndef ATOMS3_LED_PIN
#define ATOMS3_LED_PIN 35
#endif
#ifndef ATOMS3_LED_COUNT
#define ATOMS3_LED_COUNT 1
#endif
static Adafruit_NeoPixel g_led(ATOMS3_LED_COUNT, ATOMS3_LED_PIN, NEO_GRB + NEO_KHZ800);

static inline void ledShowRGB(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness = 32) {
  g_led.setBrightness(brightness);
  g_led.setPixelColor(0, g_led.Color(r, g, b));
  g_led.show();
}

static inline void ledBlueConnected() { ledShowRGB(0, 64, 255); }
static inline void ledYellowIdle()    { ledShowRGB(255, 180, 0); }
static inline void ledRedWiggler()    { ledShowRGB(255, 0, 32); }

// ---- BLE (NimBLE-Arduino recommended) ----
#include <NimBLEDevice.h>

static constexpr char SERVICE_UUID[]   = "5a1a0001-8f19-4a86-9a9e-7b4f7f9b0001";
static constexpr char KBD_CHAR_UUID[]  = "5a1a0002-8f19-4a86-9a9e-7b4f7f9b0001"; // Write (no resp)
static constexpr char MOUSE_CHAR_UUID[] = "5a1a0003-8f19-4a86-9a9e-7b4f7f9b0001"; // Write (no resp)
static constexpr char WIGGLE_CHAR_UUID[] = "5a1a0004-8f19-4a86-9a9e-7b4f7f9b0001"; // Read/Write/Notify (0/1)
static constexpr char MEDIA_CHAR_UUID[]  = "5a1a0005-8f19-4a86-9a9e-7b4f7f9b0001"; // Write (no resp) media keys
static constexpr char DEVICE_NAME[]    = "Atom HID Bridge"; // Friendly name shown in scanners/browsers

USBHIDKeyboard Keyboard;
USBHIDMouse Mouse;
USBHIDConsumerControl Consumer;
// Some USB stacks expose Consumer Control via Keyboard.consumerKey() or a separate HID class.
// USBHIDKeyboard on ESP32 Arduino supports consumer key usages via Keyboard.consumerKey(uint16_t usage).

NimBLEServer* bleServer {nullptr};
NimBLECharacteristic* kbdChar {nullptr};
NimBLECharacteristic* mouseChar {nullptr};
NimBLECharacteristic* wiggleChar {nullptr};
NimBLECharacteristic* mediaChar {nullptr};
volatile bool g_bleConnected = false;

// Mouse wiggler state
static volatile bool g_wigglerActive = false;
static uint32_t g_wiggleIntervalMs = 30000; // 30s default
static uint32_t g_lastWiggleMs = 0;
static bool g_wiggleDir = false; // toggle direction to avoid drift

static inline void updateLedState() {
  if (g_wigglerActive) { ledRedWiggler(); return; }
  if (g_bleConnected) { ledBlueConnected(); return; }
  ledYellowIdle();
}

static void setWiggler(bool on) {
  g_wigglerActive = on;
  // Reflect in characteristic and notify
  if (wiggleChar) {
    uint8_t v = g_wigglerActive ? 1 : 0;
    wiggleChar->setValue(&v, 1);
    wiggleChar->notify();
  }
  updateLedState();
}

class KbdCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    std::string s = c->getValue();
    if (s.empty()) return;
    Serial.printf("[KBD] len=%u\n", (unsigned)s.size());
    // Send: printable bytes via print(); map control chars to HID keys
    // Control handled: \b (0x08) -> Backspace, \n or \r -> Enter
    std::string acc;
    auto flush = [&]() {
      if (!acc.empty()) {
        Keyboard.print(acc.c_str());
        acc.clear();
      }
    };
    for (size_t i = 0; i < s.size(); ++i) {
      const uint8_t b = static_cast<uint8_t>(s[i]);
      if (b == 0x08) { // backspace
        flush();
        Keyboard.write(KEY_BACKSPACE);
      } else if (b == '\n' || b == '\r') {
        flush();
        Keyboard.write(KEY_RETURN);
      } else {
        acc.push_back(static_cast<char>(b));
      }
    }
    flush();
  }
};

class MouseCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    std::string v = c->getValue();
    if (v.size() < 4) return;
    const uint8_t buttons = (uint8_t)v[0];
    const int8_t  dx = (int8_t)v[1];
    const int8_t  dy = (int8_t)v[2];
    const int8_t  wheel = (int8_t)v[3];

    Serial.printf("[MOUSE] btn=%u dx=%d dy=%d wh=%d\n", buttons, dx, dy, wheel);

    // Buttons: set exact state
    if (buttons & 0x01) Mouse.press(MOUSE_LEFT); else Mouse.release(MOUSE_LEFT);
    if (buttons & 0x02) Mouse.press(MOUSE_RIGHT); else Mouse.release(MOUSE_RIGHT);
    if (buttons & 0x04) Mouse.press(MOUSE_MIDDLE); else Mouse.release(MOUSE_MIDDLE);

    // Relative move & wheel
    if (dx || dy) Mouse.move(dx, dy, 0);
    if (wheel)    Mouse.move(0, 0, wheel);
  }
};

// Media (Consumer Control) callbacks: accept 1 byte code and map to HID usages
class MediaCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    std::string v = c->getValue();
    if (v.empty()) return;
    uint8_t code = (uint8_t)v[0];
    // Map codes to standard HID Consumer usages (0x0C page)
    // Common usages: Play/Pause 0xCD, Scan Next 0xB5, Scan Previous 0xB6, Stop 0xB7,
    // Mute 0xE2, Volume Up 0xE9, Volume Down 0xEA
    uint16_t usage = 0;
    switch (code) {
      case 0x01: usage = 0x00CD; break; // Play/Pause
      case 0x02: usage = 0x00B5; break; // Next
      case 0x03: usage = 0x00B6; break; // Prev
      case 0x04: usage = 0x00B7; break; // Stop
      case 0x05: usage = 0x00E2; break; // Mute
      case 0x06: usage = 0x00E9; break; // Vol Up
      case 0x07: usage = 0x00EA; break; // Vol Down
      default: break;
    }
    if (usage) {
      Serial.printf("[MEDIA] code=0x%02X usage=0x%04X\n", code, usage);
      // Press and release consumer usage via Consumer Control interface
      Consumer.press(usage);
      delay(5);
      Consumer.release();
    }
  }
};

class WiggleCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    std::string v = c->getValue();
    bool on = false;
    if (!v.empty()) {
      // Accept single byte or ASCII '0'/'1'
      uint8_t b = (uint8_t)v[0];
      on = (b != 0 && b != '0');
    }
    setWiggler(on);
    Serial.printf("[WIGGLER] %s via BLE\n", on ? "ON" : "OFF");
  }
  void onRead(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    uint8_t v = g_wigglerActive ? 1 : 0;
    c->setValue(&v, 1);
  }
};

class ServerCallbacks : public NimBLEServerCallbacks {
  // NimBLE onConnect callback (current signature)
  void onConnect(NimBLEServer* s, NimBLEConnInfo&) override {
    g_bleConnected = true;
  updateLedState();
    Serial.println("[BLE] Connected");
  }

  // NimBLE onDisconnect callback - current signature includes reason
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo&, int reason) override {
    g_bleConnected = false;
  updateLedState();
    Serial.printf("[BLE] Disconnected (reason=%d) — advertising\n", reason);
    NimBLEDevice::startAdvertising();
  }

  // Backward-compat overload for older NimBLE versions without reason param
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo&) {
    g_bleConnected = false;
  updateLedState();
    Serial.println("[BLE] Disconnected — advertising");
    NimBLEDevice::startAdvertising();
  }
};

void setup() {
  auto cfg = M5.config(); cfg.serial_baudrate = 115200;
  M5.begin(cfg);
  delay(800);
  Serial.println("\n[BOOT] Remote HID bridge");

  // Init onboard RGB LED and set initial state (not connected)
  g_led.begin();
  ledYellowIdle();

  // USB composite: CDC + HID
  USB.begin();
  Keyboard.begin();
  Mouse.begin();
  Consumer.begin();
  Serial.println("[USB] HID ready");

  // BLE init
  NimBLEDevice::init(DEVICE_NAME);
  // Use the highest available TX power enum depending on core/library version
#if defined(ESP_PWR_LVL_P9)
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
#elif defined(ESP_PWR_LVL_P7)
  NimBLEDevice::setPower(ESP_PWR_LVL_P7);
#else
  // Fallback to library default if neither enum is defined
  // NimBLEDevice::setPower(); // uncomment if your library provides a default overload
#endif
  NimBLEDevice::setSecurityAuth(false, false, false);
  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(new ServerCallbacks());
  // Ensure we resume advertising even if callback is missed
  bleServer->advertiseOnDisconnect(true);

  NimBLEService* svc = bleServer->createService(SERVICE_UUID);

  kbdChar = svc->createCharacteristic(
    KBD_CHAR_UUID, NIMBLE_PROPERTY::WRITE_NR // write without response
  );
  kbdChar->setCallbacks(new KbdCallbacks());

  mouseChar = svc->createCharacteristic(
    MOUSE_CHAR_UUID, NIMBLE_PROPERTY::WRITE_NR
  );
  mouseChar->setCallbacks(new MouseCallbacks());

  mediaChar = svc->createCharacteristic(
    MEDIA_CHAR_UUID, NIMBLE_PROPERTY::WRITE_NR
  );
  mediaChar->setCallbacks(new MediaCallbacks());

  wiggleChar = svc->createCharacteristic(
    WIGGLE_CHAR_UUID,
    (NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY)
  );
  wiggleChar->setCallbacks(new WiggleCallbacks());
  {
    uint8_t init = 0; wiggleChar->setValue(&init, 1);
  }

  svc->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  // Build a compact, standards-compliant ADV: Flags + Name + HID appearance
  NimBLEAdvertisementData advData;
  advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  advData.setName(DEVICE_NAME);                 // Complete Local Name in ADV
  advData.setAppearance(0x03C0);                // Generic HID appearance
  adv->setAdvertisementData(advData);

  // Put the 128-bit service UUID in scan response to avoid exceeding 31 bytes
  NimBLEAdvertisementData scanData;
  scanData.addServiceUUID(SERVICE_UUID);
  adv->setScanResponseData(scanData);

  NimBLEDevice::startAdvertising();

  Serial.println("[BLE] Advertising");
}

void loop() {
  M5.update();
  // Button toggle for wiggler (single-press)
  if (M5.BtnA.wasPressed()) {
    setWiggler(!g_wigglerActive);
    Serial.printf("[WIGGLER] %s via Button\n", g_wigglerActive ? "ON" : "OFF");
  }

  // Periodic jiggle to keep host awake
  if (g_wigglerActive) {
    const uint32_t now = millis();
    if (now - g_lastWiggleMs >= g_wiggleIntervalMs) {
      g_lastWiggleMs = now;
      const int8_t step = g_wiggleDir ? 10 : -10;
      g_wiggleDir = !g_wiggleDir;
      // Two small opposite moves with a short gap to avoid visible drift
      Mouse.move(step, 0, 0);
      delay(120);
      Mouse.move(-step, 0, 0);
    }
  }

  // keep CDC alive / yield
  delay(10);
}
