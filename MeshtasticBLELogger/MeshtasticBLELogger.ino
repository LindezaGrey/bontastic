#include "NimBLEDevice.h"
#include "Arduino.h"
#include "src/protobufs/mesh.pb.h"
#include "src/nanopb/pb.h"
#include "src/nanopb/pb_encode.h"

const char *targetName = "🚐_cdb5";
const uint32_t targetPasskey = 123456;
const char *targetService = "6ba1b218-15a8-461f-9fa8-5dcae273eafd";
const char *uuidFromRadio = "2c55e69e-4993-11ed-b878-0242ac120002";
const char *uuidToRadio = "f75c76d2-129e-4dad-a1dd-7866124401e7";
const char *uuidFromNum = "ed9da18c-a800-4f66-a670-aa7547e34453";

const char *deviceInfoServiceUuid = "180a";
const char *manufacturerUuid = "2a29";
const char *modelNumberUuid = "2a24";
const char *serialNumberUuid = "2a25";
const char *hardwareRevUuid = "2a27";
const char *firmwareRevUuid = "2a26";
const char *softwareRevUuid = "2a28";

static uint32_t wantConfigId;

void drainFromRadio(NimBLERemoteCharacteristic *characteristic)
{
  if (!characteristic)
  {
    return;
  }

  while (true)
  {
    std::string packet = characteristic->readValue();
    if (packet.empty())
    {
      Serial.println("FromRadio empty");
      break;
    }
    Serial.print("FromRadio bytes ");
    Serial.println(packet.size());
  }
}

class ClientCallbacks : public NimBLEClientCallbacks
{
  void onConnect(NimBLEClient *) override
  {
    Serial.println("Connected");
  }

  void onDisconnect(NimBLEClient *, int) override
  {
    Serial.println("Disconnected");
  }

  void onPassKeyEntry(NimBLEConnInfo &info) override
  {
    Serial.println("Passkey requested");
    NimBLEDevice::injectPassKey(info, targetPasskey);
  }

  void onAuthenticationComplete(NimBLEConnInfo &) override
  {
    Serial.println("Bonded");
  }
} clientCallbacks;

void setup()
{
  Serial.begin(115200);
  while (!Serial)
  {
  }
  Serial.println("Lets Go");

  wantConfigId = millis() & 0xFFFF;

  NimBLEDevice::init("");
  NimBLEDevice::deleteAllBonds();
  Serial.println("Cleared bonds");
  NimBLEDevice::setMTU(512);
  NimBLEDevice::setSecurityAuth(true, true, false);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY);
  NimBLEDevice::setSecurityPasskey(targetPasskey);
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  NimBLEScanResults results = scan->getResults(5 * 1000, false);
  Serial.println("Scan completed");

  const NimBLEAdvertisedDevice *targetDevice = nullptr;
  for (int i = 0; i < results.getCount(); ++i)
  {
    const NimBLEAdvertisedDevice *device = results.getDevice(i);
    std::string name = device->getName();
    if (name == targetName)
    {
      targetDevice = device;
      Serial.print("Target ");
      Serial.print(device->getAddress().toString().c_str());
      Serial.print(" ");
      Serial.println(name.c_str());
      break;
    }
  }

  if (!targetDevice)
  {
    Serial.println("Target not found");
    return;
  }

  NimBLEClient *client = NimBLEDevice::createClient();
  if (!client)
  {
    Serial.println("Client create failed");
    return;
  }

  client->setClientCallbacks(&clientCallbacks, false);
  Serial.println("Connecting");
  if (!client->connect(targetDevice))
  {
    Serial.println("Connect failed");
    NimBLEDevice::deleteClient(client);
    return;
  }

  NimBLERemoteService *deviceInfo = client->getService(deviceInfoServiceUuid);
  if (deviceInfo)
  {
    Serial.println("DeviceInformationService");

    auto printChar = [&](const char *name, const char *uuid)
    {
      NimBLERemoteCharacteristic *c = deviceInfo->getCharacteristic(uuid);
      if (!c)
      {
        return;
      }
      std::string v = c->readValue();
      Serial.print(name);
      Serial.print(": ");
      Serial.println(v.c_str());
    };

    printChar("Manufacturer", manufacturerUuid);
    printChar("Model", modelNumberUuid);
    printChar("Serial", serialNumberUuid);
    printChar("HW", hardwareRevUuid);
    printChar("FW", firmwareRevUuid);
    printChar("SW", softwareRevUuid);
  }

  Serial.println("Securing");
  if (!client->secureConnection())
  {
    Serial.println("Secure start failed");
  }

  NimBLERemoteService *service = client->getService(targetService);
  if (!service)
  {
    Serial.println("Service not found");
    return;
  }

  Serial.print("Service ");
  Serial.println(targetService);

  NimBLERemoteCharacteristic *fromRadio = service->getCharacteristic(uuidFromRadio);
  NimBLERemoteCharacteristic *toRadio = service->getCharacteristic(uuidToRadio);
  NimBLERemoteCharacteristic *fromNum = service->getCharacteristic(uuidFromNum);

  if (!fromRadio || !toRadio || !fromNum)
  {
    Serial.println("Missing characteristic");
    return;
  }

  meshtastic_ToRadio req = meshtastic_ToRadio_init_zero;
  req.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
  req.want_config_id = wantConfigId++;

  uint8_t buffer[16];
  pb_ostream_t ostream = pb_ostream_from_buffer(buffer, sizeof(buffer));
  if (!pb_encode(&ostream, meshtastic_ToRadio_fields, &req))
  {
    Serial.println("Start config encode failed");
    return;
  }

  Serial.println("Request config");
  if (!toRadio->writeValue(buffer, ostream.bytes_written, true))
  {
    Serial.println("Start config failed");
    return;
  }

  Serial.println("Reading FromRadio");
  drainFromRadio(fromRadio);

  auto notifyCallback = [fromRadio](NimBLERemoteCharacteristic *characteristic, uint8_t *, size_t, bool isNotify)
  {
    if (!isNotify)
    {
      return;
    }
    Serial.print("FromNum ");
    Serial.println(characteristic->getHandle());
    drainFromRadio(fromRadio);
  };

  if (!fromNum->subscribe(true, notifyCallback, true))
  {
    Serial.println("FromNum subscribe failed");
  }
}

void loop() {}