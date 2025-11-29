#include "PrinterControl.h"
#include <Arduino.h>
#include <NimBLEAdvertising.h>
#include <NimBLEDevice.h>
#include <string>
#include "PrintHelpers.h"

extern const char *localDeviceName;

static const char *serviceUuid = "5a1a0001-8f19-4a86-9a9e-7b4f7f9b0002";
static const char *temperatureUuid = "5a1a0002-8f19-4a86-9a9e-7b4f7f9b0002";
static const char *speedUuid = "5a1a0003-8f19-4a86-9a9e-7b4f7f9b0002";
static const char *fanUuid = "5a1a0004-8f19-4a86-9a9e-7b4f7f9b0002";
static const uint16_t printerAppearance = 0x0500;

static NimBLEServer *printerServer;
static NimBLECharacteristic *temperatureChar;
static NimBLECharacteristic *speedChar;
static NimBLECharacteristic *fanChar;
static PrinterSettings printerSettings{200, 60, 120};

static void applySettings()
{
    char value[16];
    snprintf(value, sizeof(value), "%u", printerSettings.temperature);
    printInfo("TEMP", value);
    snprintf(value, sizeof(value), "%u", printerSettings.speed);
    printInfo("SPEED", value);
    snprintf(value, sizeof(value), "%u", printerSettings.fan);
    printInfo("FAN", value);
}

static NimBLECharacteristic *selectChar(uint8_t field)
{
    if (field == 0)
    {
        return temperatureChar;
    }
    if (field == 1)
    {
        return speedChar;
    }
    return fanChar;
}

static void syncField(uint8_t field, bool notify)
{
    NimBLECharacteristic *c = selectChar(field);
    if (!c)
    {
        return;
    }
    uint32_t value = field == 0 ? printerSettings.temperature : (field == 1 ? printerSettings.speed : printerSettings.fan);
    char buffer[8];
    size_t len = snprintf(buffer, sizeof(buffer), "%u", value);
    c->setValue(reinterpret_cast<uint8_t *>(buffer), len);
    if (notify)
    {
        c->notify();
    }
}

static bool clampAndStore(uint8_t field, int value)
{
    if (field == 0)
    {
        uint16_t v = constrain(value, 0, 300);
        if (printerSettings.temperature == v)
        {
            return false;
        }
        printerSettings.temperature = v;
        return true;
    }
    if (field == 1)
    {
        uint16_t v = constrain(value, 0, 200);
        if (printerSettings.speed == v)
        {
            return false;
        }
        printerSettings.speed = v;
        return true;
    }
    uint8_t v = constrain(value, 0, 255);
    if (printerSettings.fan == v)
    {
        return false;
    }
    printerSettings.fan = v;
    return true;
}

static void handleWrite(uint8_t field, const std::string &payload)
{
    if (payload.empty())
    {
        return;
    }
    int value = atoi(payload.c_str());
    if (!clampAndStore(field, value))
    {
        syncField(field, false);
        return;
    }
    syncField(field, true);
    applySettings();
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
    void onConnect(NimBLEServer *, NimBLEConnInfo &) override {}

    void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) override
    {
        NimBLEDevice::startAdvertising();
    }
};

static PrinterServerCallbacks serverCallbacks;

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
    temperatureChar = service->createCharacteristic(temperatureUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
    temperatureChar->setCallbacks(new SettingCallbacks(0));
    speedChar = service->createCharacteristic(speedUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
    speedChar->setCallbacks(new SettingCallbacks(1));
    fanChar = service->createCharacteristic(fanUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
    fanChar->setCallbacks(new SettingCallbacks(2));
    service->start();
    syncField(0, false);
    syncField(1, false);
    syncField(2, false);
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
}

const PrinterSettings &getPrinterSettings()
{
    return printerSettings;
}
