#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include "src/BleConfig.h"
#include "src/Config.h"
#include "src/EposServer.h"
#include "src/Printer.h"

EposConfig config;
String deviceIp = "0.0.0.0";
static bool httpStarted;
static bool printerReady;
static volatile bool wifiReconnectRequested;
static volatile bool testPrintRequested;

void applyPrinterConfig()
{
    if (config.rxPin == config.txPin)
    {
        config.rxPin = 1;
        config.txPin = 2;
        saveConfig();
    }
    Serial.printf("printer uart rx=%u tx=%u err=%u\n", config.rxPin, config.txPin, config.errPin);
    printer.begin(config.rxPin, config.txPin);
    printer.apply(config);
    pinMode(config.errPin, INPUT);
    Serial.println("printer config applied");
    printerReady = true;
}

void ensurePrinterReady()
{
    if (!printerReady)
        applyPrinterConfig();
}

void invalidatePrinterConfig()
{
    printerReady = false;
}

void reconnectWifi()
{
    if (!config.ssid[0])
    {
        Serial.println("wifi skipped: no ssid");
        return;
    }
    Serial.printf("wifi connecting ssid=%s\n", config.ssid);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(config.ssid, config.pass);
    for (uint8_t i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++)
    {
        Serial.print(".");
        delay(250);
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED)
    {
        deviceIp = WiFi.localIP().toString();
        Serial.printf("wifi connected ip=%s\n", deviceIp.c_str());
        notifyIp();
        if (!httpStarted)
        {
            setupEposServer();
            httpStarted = true;
            Serial.println("http server started");
        }
    }
    else
    {
        Serial.printf("wifi failed status=%d\n", WiFi.status());
    }
}

void requestWifiReconnect()
{
    wifiReconnectRequested = true;
}

void requestTestPrint()
{
    testPrintRequested = true;
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("bontastic epos boot");
    loadConfig();
    Serial.printf("config loaded ssid=%s rx=%u tx=%u heat=%u/%u/%u density=%u break=%u line=%u font=%u size=%u justify=%u charset=%u code=%u\n",
                  config.ssid, config.rxPin, config.txPin, config.heatDots, config.heatTime, config.heatInterval,
                  config.density, config.breakTime, config.lineHeight, config.font, config.size, config.justify,
                  config.charset, config.codePage);
    NimBLEDevice::init("Bontastic ePOS");
    NimBLEDevice::deleteAllBonds();
    NimBLEDevice::setMTU(512);
    NimBLEDevice::setSecurityAuth(false, false, false);
    setupBleConfig();
    Serial.println("ble config ready");
    applyPrinterConfig();
    reconnectWifi();
    Serial.println("setup done");
}

void loop()
{
    if (wifiReconnectRequested)
    {
        wifiReconnectRequested = false;
        reconnectWifi();
    }
    if (testPrintRequested)
    {
        testPrintRequested = false;
        pauseBleAdvertising();
        ensurePrinterReady();
        printer.test();
        resumeBleAdvertising();
    }
    if (httpStarted)
        handleEposServer();
    delay(2);
}
