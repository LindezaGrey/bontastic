#include "BleConfig.h"
#include <NimBLEAdvertising.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include "Config.h"
#include "Printer.h"

static const char *serviceUuid = "6d0f0001-9256-49f3-8a9b-1f0d0e905001";
static const char *ssidUuid = "6d0f0002-9256-49f3-8a9b-1f0d0e905001";
static const char *passUuid = "6d0f0003-9256-49f3-8a9b-1f0d0e905001";
static const char *ipUuid = "6d0f0004-9256-49f3-8a9b-1f0d0e905001";
static const char *settingUuid = "6d0f0005-9256-49f3-8a9b-1f0d0e905001";
static const char *actionUuid = "6d0f0006-9256-49f3-8a9b-1f0d0e905001";
static const uint16_t printerAppearance = 0x03C0;
static NimBLEServer *configServer;
static NimBLECharacteristic *ipChar;
static NimBLECharacteristic *settingChar;
static Preferences prefs;

class ServerCallbacks : public NimBLEServerCallbacks
{
    void onConnect(NimBLEServer *, NimBLEConnInfo &) override
    {
        Serial.println("ble connected");
    }

    void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) override
    {
        Serial.println("ble disconnected; advertising");
        NimBLEDevice::startAdvertising();
    }
};

static ServerCallbacks serverCallbacks;

static uint8_t asByte(const String &v, uint8_t fallback)
{
    if (!v.length())
        return fallback;
    return constrain(v.toInt(), 0, 255);
}

void loadConfig()
{
    EposConfig defaults{"", "", 1, 2, 22, 11, 120, 40, 10, 2, 30, 0, 0, 0, 2, 23};
    config = defaults;
    Serial.println("nvs open read");
    prefs.begin("epos", true);
    prefs.getString("ssid", "").toCharArray(config.ssid, sizeof(config.ssid));
    prefs.getString("pass", "").toCharArray(config.pass, sizeof(config.pass));
    config.rxPin = prefs.getUChar("rx", config.rxPin);
    config.txPin = prefs.getUChar("tx", config.txPin);
    config.errPin = prefs.getUChar("err", config.errPin);
    config.heatDots = prefs.getUChar("hd", config.heatDots);
    config.heatTime = prefs.getUChar("ht", config.heatTime);
    config.heatInterval = prefs.getUChar("hi", config.heatInterval);
    config.density = prefs.getUChar("den", config.density);
    config.breakTime = prefs.getUChar("brk", config.breakTime);
    config.lineHeight = prefs.getUChar("line", config.lineHeight);
    config.font = prefs.getUChar("font", config.font);
    config.size = prefs.getUChar("size", config.size);
    config.justify = prefs.getUChar("just", config.justify);
    config.charset = prefs.getUChar("chars", config.charset);
    config.codePage = prefs.getUChar("code", config.codePage);
    prefs.end();
    if (config.rxPin == config.txPin)
    {
        config.rxPin = defaults.rxPin;
        config.txPin = defaults.txPin;
        prefs.begin("epos", false);
        prefs.putUChar("rx", config.rxPin);
        prefs.putUChar("tx", config.txPin);
        prefs.end();
    }
    Serial.println("nvs read done");
}

static String settingsValue()
{
    String v;
    v.reserve(120);
    v += "rx=";
    v += String(config.rxPin);
    v += ";tx=";
    v += String(config.txPin);
    v += ";err=";
    v += String(config.errPin);
    v += ";heatDots=";
    v += String(config.heatDots);
    v += ";heatTime=";
    v += String(config.heatTime);
    v += ";heatInterval=";
    v += String(config.heatInterval);
    v += ";density=";
    v += String(config.density);
    v += ";breakTime=";
    v += String(config.breakTime);
    v += ";lineHeight=";
    v += String(config.lineHeight);
    v += ";font=";
    v += String(config.font);
    v += ";size=";
    v += String(config.size);
    v += ";justify=";
    v += String(config.justify);
    return v;
}

static void syncSettings()
{
    if (!settingChar)
        return;
    String v = settingsValue();
    settingChar->setValue(v.c_str());
    settingChar->notify();
}

void saveConfig()
{
    Serial.println("nvs open write");
    prefs.begin("epos", false);
    prefs.putString("ssid", config.ssid);
    prefs.putString("pass", config.pass);
    prefs.putUChar("rx", config.rxPin);
    prefs.putUChar("tx", config.txPin);
    prefs.putUChar("err", config.errPin);
    prefs.putUChar("hd", config.heatDots);
    prefs.putUChar("ht", config.heatTime);
    prefs.putUChar("hi", config.heatInterval);
    prefs.putUChar("den", config.density);
    prefs.putUChar("brk", config.breakTime);
    prefs.putUChar("line", config.lineHeight);
    prefs.putUChar("font", config.font);
    prefs.putUChar("size", config.size);
    prefs.putUChar("just", config.justify);
    prefs.putUChar("chars", config.charset);
    prefs.putUChar("code", config.codePage);
    prefs.end();
    Serial.println("nvs write done");
}

static void updateSetting(const String &body)
{
    int split = body.indexOf('=');
    if (split < 1)
        return;
    String key = body.substring(0, split);
    String value = body.substring(split + 1);
    if (key == "rx")
    {
        uint8_t pin = asByte(value, config.rxPin);
        if (pin != config.txPin)
            config.rxPin = pin;
    }
    else if (key == "tx")
    {
        uint8_t pin = asByte(value, config.txPin);
        if (pin != config.rxPin)
            config.txPin = pin;
    }
    else if (key == "err")
        config.errPin = asByte(value, config.errPin);
    else if (key == "heatDots")
        config.heatDots = asByte(value, config.heatDots);
    else if (key == "heatTime")
        config.heatTime = asByte(value, config.heatTime);
    else if (key == "heatInterval")
        config.heatInterval = asByte(value, config.heatInterval);
    else if (key == "density")
        config.density = asByte(value, config.density);
    else if (key == "breakTime")
        config.breakTime = asByte(value, config.breakTime);
    else if (key == "lineHeight")
        config.lineHeight = asByte(value, config.lineHeight);
    else if (key == "font")
        config.font = asByte(value, config.font);
    else if (key == "size")
        config.size = asByte(value, config.size);
    else if (key == "justify")
        config.justify = asByte(value, config.justify);
    else if (key == "charset")
        config.charset = asByte(value, config.charset);
    else if (key == "codePage")
        config.codePage = asByte(value, config.codePage);
    Serial.printf("setting %s=%s\n", key.c_str(), value.c_str());
    invalidatePrinterConfig();
    saveConfig();
    syncSettings();
}

class WriteCallbacks : public NimBLECharacteristicCallbacks
{
    void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) override
    {
        String v = c->getValue().c_str();
        String uuid = c->getUUID().toString().c_str();
        Serial.printf("ble write uuid=%s len=%u\n", uuid.c_str(), (unsigned)v.length());
        if (uuid == ssidUuid)
        {
            v.toCharArray(config.ssid, sizeof(config.ssid));
            Serial.printf("ssid set %s\n", config.ssid);
        }
        else if (uuid == passUuid)
        {
            v.toCharArray(config.pass, sizeof(config.pass));
            Serial.println("pass set");
        }
        else if (uuid == settingUuid)
        {
            updateSetting(v);
            return;
        }
        else if (uuid == actionUuid && v == "reboot")
        {
            Serial.println("reboot requested");
            ESP.restart();
        }
        else if (uuid == actionUuid && v == "connect")
        {
            Serial.println("wifi connect requested");
            requestWifiReconnect();
            return;
        }
        else if (uuid == actionUuid && v == "test")
        {
            Serial.println("test print requested");
            requestTestPrint();
            return;
        }
        saveConfig();
    }
};

static WriteCallbacks writeCallbacks;

void notifyIp()
{
    if (!ipChar)
        return;
    Serial.printf("ble notify ip=%s\n", deviceIp.c_str());
    ipChar->setValue(deviceIp.c_str());
    ipChar->notify();
}

void pauseBleAdvertising()
{
    Serial.println("ble advertising pause");
    NimBLEDevice::stopAdvertising();
}

void resumeBleAdvertising()
{
    Serial.println("ble advertising resume");
    NimBLEDevice::startAdvertising();
}

void setupBleConfig()
{
    if (configServer)
    {
        Serial.println("ble already ready");
        return;
    }
    Serial.println("ble init");
    configServer = NimBLEDevice::createServer();
    configServer->setCallbacks(&serverCallbacks, false);
    configServer->advertiseOnDisconnect(true);
    NimBLEService *service = configServer->createService(serviceUuid);
    NimBLECharacteristic *ssid = service->createCharacteristic(ssidUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
    NimBLECharacteristic *pass = service->createCharacteristic(passUuid, NIMBLE_PROPERTY::WRITE);
    ipChar = service->createCharacteristic(ipUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    settingChar = service->createCharacteristic(settingUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
    NimBLECharacteristic *action = service->createCharacteristic(actionUuid, NIMBLE_PROPERTY::WRITE);
    Serial.println("ble characteristics created");
    ssid->setValue(config.ssid);
    ipChar->setValue(deviceIp.c_str());
    ssid->setCallbacks(&writeCallbacks);
    pass->setCallbacks(&writeCallbacks);
    String initialSettings = settingsValue();
    settingChar->setValue(initialSettings.c_str());
    settingChar->setCallbacks(&writeCallbacks);
    action->setCallbacks(&writeCallbacks);
    service->start();
    Serial.println("ble service started");
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    NimBLEAdvertisementData advData;
    advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    advData.setAppearance(printerAppearance);
    advData.addServiceUUID(serviceUuid);
    adv->setAdvertisementData(advData);
    NimBLEAdvertisementData scanData;
    scanData.setName("Bontastic ePOS");
    adv->setScanResponseData(scanData);
    NimBLEDevice::startAdvertising();
    Serial.println("ble advertising name=Bontastic ePOS service=6d0f0001-9256-49f3-8a9b-1f0d0e905001");
}
