#include "PrinterControl.h"
#include <Arduino.h>
#include <NimBLEAdvertising.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <string>
#include "PrintHelpers.h"
#include "Bontastic_Thermal.h"
#include "MeshtasticBLELogger.h"

extern const char *localDeviceName;
extern Bontastic_Thermal printer;
extern volatile bool meshtasticConnected;

static const char *serviceUuid = "5a1a0001-8f19-4a86-9a9e-7b4f7f9b0002";
static const char *fieldUuids[] = {
    "5a1a0002-8f19-4a86-9a9e-7b4f7f9b0002",
    "5a1a0003-8f19-4a86-9a9e-7b4f7f9b0002",
    "5a1a0004-8f19-4a86-9a9e-7b4f7f9b0002",
    "5a1a0005-8f19-4a86-9a9e-7b4f7f9b0002",
    "5a1a0006-8f19-4a86-9a9e-7b4f7f9b0002",
    "5a1a0007-8f19-4a86-9a9e-7b4f7f9b0002",
    "5a1a0008-8f19-4a86-9a9e-7b4f7f9b0002",
    "5a1a0009-8f19-4a86-9a9e-7b4f7f9b0002",
    "5a1a000a-8f19-4a86-9a9e-7b4f7f9b0002",
    "5a1a000b-8f19-4a86-9a9e-7b4f7f9b0002",
    "5a1a000c-8f19-4a86-9a9e-7b4f7f9b0002",
    "5a1a000d-8f19-4a86-9a9e-7b4f7f9b0002",
    "5a1a000e-8f19-4a86-9a9e-7b4f7f9b0002",
    "5a1a000f-8f19-4a86-9a9e-7b4f7f9b0002",
    "5a1a0010-8f19-4a86-9a9e-7b4f7f9b0002",
    "5a1a0011-8f19-4a86-9a9e-7b4f7f9b0002",
    "5a1a0012-8f19-4a86-9a9e-7b4f7f9b0002",
    "5a1a0013-8f19-4a86-9a9e-7b4f7f9b0002",
    "5a1a0014-8f19-4a86-9a9e-7b4f7f9b0002"};

enum SettingField : uint8_t
{
    HeatDots,
    HeatTime,
    HeatInterval,
    Density,
    BreakTime,
    LineHeight,
    FontSelect,
    SizeSelect,
    JustifySelect,
    Decorations,
    Feed,
    Charset,
    CodePage,
    PrintText,
    MeshName,
    MeshPin,
    PrinterRxPin,
    PrinterTxPin,
    PrintQr,
    FieldCount
};

static const uint16_t printerAppearance = 0x03C0;

static const char *meshLinkUuid = "5a1a0015-8f19-4a86-9a9e-7b4f7f9b0002";
static const char *bitmapUuid = "5a1a0016-8f19-4a86-9a9e-7b4f7f9b0002";
static const char *logUuid = "5a1a0017-8f19-4a86-9a9e-7b4f7f9b0002";

static NimBLEServer *printerServer;
static NimBLECharacteristic *characteristics[FieldCount];
static NimBLECharacteristic *meshLinkCharacteristic;
static NimBLECharacteristic *bitmapCharacteristic;
static NimBLECharacteristic *logCharacteristic;
static bool lastMeshLink;
static const PrinterSettings defaultSettings{11, 120, 40, 10, 2, 30, 0, 0, 0, 0, 0, 2, 23, "MO1_1dfd", "123456", 1, 2};
static PrinterSettings printerSettings = defaultSettings;
static Preferences printerPrefs;
static bool prefsReady;

static uint16_t bitmapHeight;
static uint32_t bitmapExpected;
static uint32_t bitmapReceived;
static std::string bitmapBuffer;
static const uint16_t bitmapWidth = 384;
static uint8_t bitmapHeader[8];
static uint8_t bitmapHeaderReceived;
static uint32_t bitmapLastLog;

static void handleBitmapChunk(const uint8_t *data, size_t len)
{
    if (!data || len == 0)
    {
        return;
    }

    if (bitmapExpected != 0 && len >= 2 && data[0] == 0x42 && data[1] == 0x4d)
    {
        bleLog("BMP restart");
        bitmapExpected = 0;
        bitmapReceived = 0;
        bitmapHeaderReceived = 0;
        bitmapBuffer.clear();
    }

    if (bitmapExpected == 0)
    {
        while (bitmapHeaderReceived < sizeof(bitmapHeader) && len > 0)
        {
            bitmapHeader[bitmapHeaderReceived++] = *data++;
            len--;
        }
        if (bitmapHeaderReceived < sizeof(bitmapHeader))
        {
            return;
        }
        if (bitmapHeader[0] != 0x42 || bitmapHeader[1] != 0x4d)
        {
            bitmapHeaderReceived = 0;
            return;
        }
        bitmapHeight = (uint16_t)bitmapHeader[2] | ((uint16_t)bitmapHeader[3] << 8);
        bitmapExpected = (uint32_t)bitmapHeader[4] | ((uint32_t)bitmapHeader[5] << 8) | ((uint32_t)bitmapHeader[6] << 16) | ((uint32_t)bitmapHeader[7] << 24);
        bitmapReceived = 0;
        bitmapLastLog = 0;
        bitmapBuffer.clear();
        bitmapBuffer.reserve(bitmapExpected);
        bleLogf("BMP start h=%u bytes=%lu", (unsigned)bitmapHeight, (unsigned long)bitmapExpected);
    }

    if (bitmapExpected == 0)
    {
        return;
    }

    size_t take = len;
    if (bitmapReceived + take > bitmapExpected)
    {
        take = bitmapExpected - bitmapReceived;
    }
    if (take > 0)
    {
        bitmapBuffer.append(reinterpret_cast<const char *>(data), take);
        bitmapReceived += take;
    }

    if (bitmapReceived == bitmapExpected)
    {
        bleLog("BMP print");
        bool upsideDown = (printerSettings.decorations & 0x10) != 0;
        gsV0WithUpsideDown(bitmapWidth / 8, bitmapHeight,
                           reinterpret_cast<const uint8_t *>(bitmapBuffer.data()), bitmapBuffer.size(), upsideDown);
        printer.feed(2);
        bleLog("BMP done");
        bitmapExpected = 0;
        bitmapReceived = 0;
        bitmapHeaderReceived = 0;
        bitmapBuffer.clear();
    }
}

static void syncMeshLink(bool notify)
{
    if (!meshLinkCharacteristic)
    {
        return;
    }
    meshLinkCharacteristic->setValue(meshtasticConnected ? "1" : "0");
    if (notify)
    {
        meshLinkCharacteristic->notify();
    }
}

static const char *fieldLabel(uint8_t field)
{
    switch (field)
    {
    case HeatDots:
        return "HEAT_DOTS";
    case HeatTime:
        return "HEAT_TIME";
    case HeatInterval:
        return "HEAT_INTERVAL";
    case Density:
        return "DENSITY";
    case BreakTime:
        return "BREAK";
    case LineHeight:
        return "LINE_HEIGHT";
    case FontSelect:
        return "FONT";
    case SizeSelect:
        return "SIZE";
    case JustifySelect:
        return "JUSTIFY";
    case Decorations:
        return "DECOR";
    case Feed:
        return "FEED";
    case Charset:
        return "CHARSET";
    case CodePage:
        return "CODEPAGE";
    case PrintText:
        return "PRINT";
    case MeshName:
        return "MESH_NAME";
    case MeshPin:
        return "MESH_PIN";
    case PrinterRxPin:
        return "PRINTER_RX";
    case PrinterTxPin:
        return "PRINTER_TX";
    case PrintQr:
        return "PRINT_QR";
    default:
        return nullptr;
    }
}

static const char *fieldKeys[] = {
    "heatDots",
    "heatTime",
    "heatInterval",
    "density",
    "breakTime",
    "lineHeight",
    "font",
    "size",
    "justify",
    "decorations",
    nullptr,
    "charset",
    "codePage",
    nullptr,
    "meshName",
    "meshPin",
    "printerRxPin",
    "printerTxPin",
    nullptr};

static void *fieldSlot(uint8_t field);

static void ensurePrefs()
{
    if (!prefsReady)
    {
        prefsReady = printerPrefs.begin("printer", false);
    }
}

static void loadSettings()
{
    printerSettings = defaultSettings;
    ensurePrefs();
    if (!prefsReady)
    {
        return;
    }
    for (uint8_t i = 0; i < FieldCount; ++i)
    {
        const char *key = fieldKeys[i];
        if (!key)
        {
            continue;
        }
        void *slot = fieldSlot(i);
        if (!slot)
        {
            continue;
        }
        if (i == MeshName || i == MeshPin)
        {
            String val = printerPrefs.getString(key, i == MeshName ? defaultSettings.meshName : defaultSettings.meshPin);
            strlcpy((char *)slot, val.c_str(), i == MeshName ? sizeof(printerSettings.meshName) : sizeof(printerSettings.meshPin));
        }
        else
        {
            *(uint8_t *)slot = printerPrefs.getUChar(key, *(uint8_t *)slot);
        }
    }
}

static void persistField(uint8_t field)
{
    const char *key = field < FieldCount ? fieldKeys[field] : nullptr;
    if (!key)
    {
        return;
    }
    ensurePrefs();
    if (!prefsReady)
    {
        return;
    }
    void *slot = fieldSlot(field);
    if (!slot)
    {
        return;
    }
    if (field == MeshName || field == MeshPin)
    {
        printerPrefs.putString(key, (char *)slot);
    }
    else
    {
        printerPrefs.putUChar(key, *(uint8_t *)slot);
    }
}

static void *fieldSlot(uint8_t field)
{
    switch (field)
    {
    case HeatDots:
        return &printerSettings.heatDots;
    case HeatTime:
        return &printerSettings.heatTime;
    case HeatInterval:
        return &printerSettings.heatInterval;
    case Density:
        return &printerSettings.density;
    case BreakTime:
        return &printerSettings.breakTime;
    case LineHeight:
        return &printerSettings.lineHeight;
    case FontSelect:
        return &printerSettings.font;
    case SizeSelect:
        return &printerSettings.size;
    case JustifySelect:
        return &printerSettings.justify;
    case Decorations:
        return &printerSettings.decorations;
    case Feed:
        return &printerSettings.feedRows;
    case Charset:
        return &printerSettings.charset;
    case CodePage:
        return &printerSettings.codePage;
    case MeshName:
        return printerSettings.meshName;
    case MeshPin:
        return printerSettings.meshPin;
    case PrinterRxPin:
        return &printerSettings.printerRxPin;
    case PrinterTxPin:
        return &printerSettings.printerTxPin;
    case PrintText:
    case PrintQr:
        return nullptr;
    default:
        return nullptr;
    }
}

static uint16_t clampField(uint8_t field, int value)
{
    switch (field)
    {
    case HeatDots:
        return constrain(value, 0, 15);
    case HeatTime:
        return constrain(value, 0, 255);
    case HeatInterval:
        return constrain(value, 0, 255);
    case Density:
        return constrain(value, 0, 31);
    case BreakTime:
        return constrain(value, 0, 7);
    case LineHeight:
        return constrain(value, 24, 64);
    case FontSelect:
        return constrain(value, 0, 1);
    case SizeSelect:
        return constrain(value, 0, 2);
    case JustifySelect:
        return constrain(value, 0, 2);
    case Decorations:
        return constrain(value, 0, 31);
    case Feed:
        return constrain(value, 0, 50);
    case Charset:
        return constrain(value, 0, 15);
    case CodePage:
        return constrain(value, 0, 47);
    case PrinterRxPin:
    case PrinterTxPin:
        return constrain(value, 0, 40);
    case PrintText:
    case PrintQr:
        return 0;
    default:
        return value < 0 ? 0 : value;
    }
}

static NimBLECharacteristic *getCharacteristic(uint8_t field)
{
    if (field >= FieldCount)
    {
        return nullptr;
    }
    return characteristics[field];
}

static void syncField(uint8_t field, bool notify)
{
    NimBLECharacteristic *c = getCharacteristic(field);
    if (!c)
    {
        return;
    }
    void *slot = fieldSlot(field);
    if (field == MeshName || field == MeshPin)
    {
        c->setValue(std::string((char *)slot));
    }
    else
    {
        uint8_t val = slot ? *(uint8_t *)slot : 0;
        char buffer[8];
        size_t len = snprintf(buffer, sizeof(buffer), "%u", val);
        c->setValue(reinterpret_cast<uint8_t *>(buffer), len);
    }

    if (notify)
    {
        c->notify();
    }
}

static void logField(uint8_t field, uint16_t value)
{
    const char *label = fieldLabel(field);
    if (!label)
    {
        return;
    }
    if (field == MeshName || field == MeshPin)
    {
        printInfo(label, (char *)fieldSlot(field));
    }
    else
    {
        char text[12];
        snprintf(text, sizeof(text), "%u", value);
        printInfo(label, text);
    }
}

static void applyPrinterConfig()
{
    printer.setHeatConfig(printerSettings.heatDots, printerSettings.heatTime, printerSettings.heatInterval);
    printer.setPrintDensity(printerSettings.density, printerSettings.breakTime);
    printer.setLineHeight(printerSettings.lineHeight);
    printer.setCharset(printerSettings.charset);
    printer.setCodePage(printerSettings.codePage);
    printer.setFont(printerSettings.font ? 'B' : 'A');
    printer.setSize(printerSettings.size == 0 ? 'S' : (printerSettings.size == 1 ? 'M' : 'L'));
    printer.justify(printerSettings.justify == 0 ? 'L' : (printerSettings.justify == 1 ? 'C' : 'R'));
    bool bold = printerSettings.decorations & 0x01;
    bool inverse = printerSettings.decorations & 0x02;
    bool strike = printerSettings.decorations & 0x04;
    bool doubleWidth = printerSettings.decorations & 0x08;
    bool upsideDown = printerSettings.decorations & 0x10;
    if (bold)
    {
        printer.boldOn();
    }
    else
    {
        printer.boldOff();
    }
    if (inverse)
    {
        printer.inverseOn();
    }
    else
    {
        printer.inverseOff();
    }
    if (strike)
    {
        printer.strikeOn();
    }
    else
    {
        printer.strikeOff();
    }
    if (doubleWidth)
    {
        printer.doubleWidthOn();
    }
    else
    {
        printer.doubleWidthOff();
    }

    if (upsideDown)
    {
        printer.upsideDownOn();
    }
    else
    {
        printer.upsideDownOff();
    }
}

void applyPrinterSettings()
{
    applyPrinterConfig();
}

static void applyFeed()
{
    uint8_t rows = printerSettings.feedRows;
    if (!rows)
    {
        return;
    }
    printer.feed(rows);
    printerSettings.feedRows = 0;
    syncField(Feed, true);
}

static void handleWrite(uint8_t field, const std::string &payload)
{
    if (payload.empty())
    {
        return;
    }
    if (field == PrintText)
    {
        std::string processed = processTextForPrinter(payload);
        printStyledText(processed);
        printer.feed(2);
        return;
    }
    if (field == PrintQr)
    {
        printer.qrSelectModel(2);
        printer.qrSetModuleSize(4);
        printer.qrSetErrorCorrection(48);
        printer.qrStoreData(reinterpret_cast<const uint8_t *>(payload.data()), payload.size());
        printer.qrPrint();
        printer.feed(2);
        return;
    }
    if (field == MeshName || field == MeshPin)
    {
        char *slot = (char *)fieldSlot(field);
        size_t maxLen = (field == MeshName) ? sizeof(printerSettings.meshName) : sizeof(printerSettings.meshPin);
        strlcpy(slot, payload.c_str(), maxLen);
        syncField(field, true);
        persistField(field);
        logField(field, 0);
        return;
    }

    int value = atoi(payload.c_str());
    uint16_t clamped = clampField(field, value);
    if (field == Feed)
    {
        printerSettings.feedRows = static_cast<uint8_t>(clamped);
        logField(field, clamped);
        applyFeed();
        return;
    }
    uint8_t *slot = (uint8_t *)fieldSlot(field);
    if (!slot)
    {
        return;
    }
    if (*slot == clamped)
    {
        syncField(field, false);
        return;
    }
    *slot = static_cast<uint8_t>(clamped);
    syncField(field, true);
    if (field == PrinterRxPin || field == PrinterTxPin)
    {
        updatePrinterPins(printerSettings.printerRxPin, printerSettings.printerTxPin);
    }
    else
    {
        applyPrinterConfig();
    }
    persistField(field);
    logField(field, clamped);
}

class SettingCallbacks : public NimBLECharacteristicCallbacks
{
public:
    explicit SettingCallbacks(uint8_t f) : field(f) {}

private:
    uint8_t field;

    void onRead(NimBLECharacteristic *, NimBLEConnInfo &) override
    {
        syncField(field, false);
    }

    void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) override
    {
        handleWrite(field, c->getValue());
    }
};

class PrinterServerCallbacks : public NimBLEServerCallbacks
{
    void onConnect(NimBLEServer *, NimBLEConnInfo &) override
    {
        bleLogSetEnabled(true);
    }

    void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) override
    {
        bleLogSetEnabled(false);
        NimBLEDevice::startAdvertising();
    }
};

static PrinterServerCallbacks serverCallbacks;

class BitmapCallbacks : public NimBLECharacteristicCallbacks
{
    void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) override
    {
        const std::string &v = c->getValue();
        if (v.empty())
        {
            return;
        }
        handleBitmapChunk(reinterpret_cast<const uint8_t *>(v.data()), v.size());
    }
};

static BitmapCallbacks bitmapCallbacks;

void setupPrinterControl()
{
    if (printerServer)
    {
        return;
    }
    printerServer = NimBLEDevice::createServer();
    printerServer->setCallbacks(&serverCallbacks, false);
    printerServer->advertiseOnDisconnect(true);

    NimBLEService *service = printerServer->createService(serviceUuid);
    for (uint8_t i = 0; i < FieldCount; ++i)
    {
        if (i == PrintText || i == PrintQr)
        {
            characteristics[i] = service->createCharacteristic(fieldUuids[i], NIMBLE_PROPERTY::WRITE);
        }
        else
        {
            characteristics[i] = service->createCharacteristic(fieldUuids[i], NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
        }
        characteristics[i]->setCallbacks(new SettingCallbacks(i));
    }

    meshLinkCharacteristic = service->createCharacteristic(meshLinkUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    lastMeshLink = meshtasticConnected;
    syncMeshLink(false);

    bitmapCharacteristic = service->createCharacteristic(bitmapUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    bitmapCharacteristic->setCallbacks(&bitmapCallbacks);

    logCharacteristic = service->createCharacteristic(logUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    bleLogAttachCharacteristic(logCharacteristic);

    service->start();
    loadSettings();
    applyPrinterConfig();
    for (uint8_t i = 0; i < FieldCount; ++i)
    {
        syncField(i, false);
    }
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    NimBLEAdvertisementData advData;
    advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    advData.setAppearance(printerAppearance);
    advData.setName(localDeviceName ? localDeviceName : "Bontastic Printer");
    advData.addServiceUUID(serviceUuid);
    adv->setAdvertisementData(advData);

    NimBLEAdvertisementData scanData;
    scanData.addServiceUUID(serviceUuid);
    adv->setScanResponseData(scanData);

    NimBLEDevice::startAdvertising();
}

void printerControlLoop()
{
    bool linked = meshtasticConnected;
    if (linked != lastMeshLink)
    {
        lastMeshLink = linked;
        syncMeshLink(true);
    }
}

const PrinterSettings &getPrinterSettings()
{
    return printerSettings;
}
