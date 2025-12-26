#include "NimBLEDevice.h"
#include "Arduino.h"
#include "src/protobufs/mesh.pb.h"
#include "src/protobufs/portnums.pb.h"
#include "src/nanopb/pb.h"
#include "src/nanopb/pb_decode.h"
#include "src/nanopb/pb_encode.h"

#include "src/printer/PrintHelpers.h"
#include "src/printer/PrinterControl.h"
#include "src/printer/MeshtasticBLELogger.h"

#define ENABLE_CONTEST_QR_MODULE
#ifdef ENABLE_CONTEST_QR_MODULE
#include "src/printer/ContestQrModule.h"
#endif

#include <map>
#include <string>

std::map<uint32_t, std::string> nodeNames;
std::map<std::string, int> channelIndices;

const char *localDeviceName = "Bontastic Printer";

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
static NimBLERemoteCharacteristic *fromRadio;
static NimBLERemoteCharacteristic *toRadio;
static NimBLERemoteCharacteristic *fromNum;
static volatile bool fromRadioPending;
static const int maxNotifyQueue = 8;
static volatile int notifyQueueCount;
static volatile bool configComplete = false;

volatile bool meshtasticConnected;

bool readFromRadioPacket(std::string &packet)
{
  packet = fromRadio->readValue();
  return !packet.empty();
}

void decodeFromRadioPacket(const std::string &packet)
{
  meshtastic_FromRadio msg = meshtastic_FromRadio_init_zero;
  pb_istream_t stream = pb_istream_from_buffer(reinterpret_cast<const uint8_t *>(packet.data()), packet.size());
  if (!pb_decode(&stream, meshtastic_FromRadio_fields, &msg))
  {
    bleLog("FromRadio decode failed");
    return;
  }

  if (msg.which_payload_variant == meshtastic_FromRadio_packet_tag)
  {
    const meshtastic_Data &d = msg.packet.decoded;
    bleLogf("FromRadio port=%u len=%u", (unsigned)d.portnum, (unsigned)d.payload.size);

    switch (d.portnum)
    {
    case meshtastic_PortNum_TEXT_MESSAGE_APP:
      if (d.payload.size > 0)
      {
        std::string senderName = "Unknown";
        if (nodeNames.count(msg.packet.from))
        {
          senderName = nodeNames[msg.packet.from];
        }
        else
        {
          char buf[16];
          snprintf(buf, sizeof(buf), "!%08x", msg.packet.from);
          senderName = buf;
        }
        printTextMessage(d.payload.bytes, d.payload.size, senderName.c_str(), msg.packet.rx_time);
      }
      break;

    case meshtastic_PortNum_POSITION_APP:
    {
      meshtastic_Position position = meshtastic_Position_init_zero;
      pb_istream_t ps = pb_istream_from_buffer(d.payload.bytes, d.payload.size);
      if (pb_decode(&ps, meshtastic_Position_fields, &position))
      {
        printPosition(position.latitude_i / 1e7, position.longitude_i / 1e7, position.altitude);
      }
      else
      {
        bleLog("POS decode fail");
      }
      break;
    }

    case meshtastic_PortNum_NODEINFO_APP:
    {
      meshtastic_User user = meshtastic_User_init_zero;
      pb_istream_t ns = pb_istream_from_buffer(d.payload.bytes, d.payload.size);
      if (pb_decode(&ns, meshtastic_User_fields, &user))
      {
        // printNodeInfo(msg.packet.from, user.long_name);
        nodeNames[msg.packet.from] = user.long_name;
      }
      else
      {
        bleLog("NODE decode fail");
      }
      break;
    }

    default:
      printBinaryPayload(d.payload.bytes, d.payload.size);
      break;
    }
  }
  else if (msg.which_payload_variant == meshtastic_FromRadio_channel_tag)
  {
    const meshtastic_Channel &c = msg.channel;
    if (c.settings.name[0] != 0)
    {
      channelIndices[c.settings.name] = c.index;
      bleLogf("Channel %s index %d", c.settings.name, c.index);
    }
  }
  else if (msg.which_payload_variant == meshtastic_FromRadio_config_complete_id_tag)
  {
    if (msg.config_complete_id == wantConfigId)
    {
      configComplete = true;
      bleLog("Config complete");
    }
  }
}

void drainFromRadio()
{
  while (true)
  {
    std::string packet;
    if (!readFromRadioPacket(packet))
    {
      break;
    }
    bleLogf("FromRadio bytes=%u", (unsigned)packet.size());
    decodeFromRadioPacket(packet);
  }
}

class ClientCallbacks : public NimBLEClientCallbacks
{
  void onConnect(NimBLEClient *) override
  {
    meshtasticConnected = true;
    bleLog("Meshtastic connected");
  }

  void onDisconnect(NimBLEClient *, int) override
  {
    meshtasticConnected = false;
    bleLog("Meshtastic disconnected");
  }

  void onPassKeyEntry(NimBLEConnInfo &info) override
  {
    bleLog("Passkey requested");
    uint32_t passkey = atoi(getPrinterSettings().meshPin);
    NimBLEDevice::injectPassKey(info, passkey);
  }

  void onAuthenticationComplete(NimBLEConnInfo &) override
  {
    bleLog("Bonded");
  }
} clientCallbacks;

void setup()
{
  wantConfigId = millis() & 0xFFFF;

  NimBLEDevice::init(localDeviceName);
  setupPrinterControl();
  printerSetup();
  bleLog("Boot");

#ifdef ENABLE_CONTEST_QR_MODULE
  contestQrSetup();
  contestQrSetIntervalMinutes(15);
  contestQrSetContent("https://meshtastic.org/e/?add=true#CjESILQC2idq9-coIo9Sggdz78UgpetPU2o7-F2ITBLHMOyWGglib250YXN0aWMoATABEg8IATgDQANIAVAbaAHABgE");
#endif

  NimBLEDevice::deleteAllBonds();
  bleLog("Cleared bonds");
  NimBLEDevice::setMTU(512);
  NimBLEDevice::setSecurityAuth(false, false, false);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY);
  NimBLEDevice::setSecurityPasskey(atoi(getPrinterSettings().meshPin));

  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  NimBLEScanResults results = scan->getResults(5 * 1000, false);
  bleLog("Scan completed");

  const NimBLEAdvertisedDevice *targetDevice = nullptr;
  const char *targetName = getPrinterSettings().meshName;
  for (int i = 0; i < results.getCount(); ++i)
  {
    const NimBLEAdvertisedDevice *device = results.getDevice(i);
    std::string name = device->getName();
    if (name == targetName)
    {
      targetDevice = device;
      bleLogf("Target %s %s", device->getAddress().toString().c_str(), name.c_str());
      break;
    }
  }

  if (!targetDevice)
  {
    bleLog("Target not found");
    return;
  }

  NimBLEClient *client = NimBLEDevice::createClient();
  if (!client)
  {
    bleLog("Client create failed");
    return;
  }

  client->setClientCallbacks(&clientCallbacks, false);
  bleLog("Connecting");
  if (!client->connect(targetDevice))
  {
    bleLog("Connect failed");
    NimBLEDevice::deleteClient(client);
    return;
  }

  NimBLERemoteService *deviceInfo = client->getService(deviceInfoServiceUuid);
  if (deviceInfo)
  {
    bleLog("DeviceInformationService");

    auto printChar = [&](const char *name, const char *uuid)
    {
      NimBLERemoteCharacteristic *c = deviceInfo->getCharacteristic(uuid);
      if (!c)
      {
        return;
      }
      std::string v = c->readValue();
      printInfo(name, v.c_str());
    };

    printChar("Manufacturer", manufacturerUuid);
    printChar("Model", modelNumberUuid);
    printChar("Serial", serialNumberUuid);
    printChar("HW", hardwareRevUuid);
    printChar("FW", firmwareRevUuid);
    printChar("SW", softwareRevUuid);
  }

  bleLog("Securing");
  if (!client->secureConnection())
  {
    bleLog("Secure start failed");
  }

  NimBLERemoteService *service = client->getService(targetService);
  if (!service)
  {
    bleLog("Service not found");
    return;
  }

  bleLogf("Service %s", targetService);

  fromRadio = service->getCharacteristic(uuidFromRadio);
  toRadio = service->getCharacteristic(uuidToRadio);
  fromNum = service->getCharacteristic(uuidFromNum);

  meshtastic_ToRadio req = meshtastic_ToRadio_init_zero;
  req.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
  req.want_config_id = wantConfigId++;

  uint8_t buffer[16];
  pb_ostream_t ostream = pb_ostream_from_buffer(buffer, sizeof(buffer));
  if (!pb_encode(&ostream, meshtastic_ToRadio_fields, &req))
  {
    bleLog("Start config encode failed");
    return;
  }

  bleLog("Request config");
  if (!toRadio->writeValue(buffer, ostream.bytes_written, true))
  {
    bleLog("Start config failed");
    return;
  }

  bleLog("Reading FromRadio");
  unsigned long startConfig = millis();
  while (!configComplete && millis() - startConfig < 10000)
  {
    drainFromRadio();
    delay(100);
  }

  printStartupLogo();

  auto notifyCallback = [](NimBLERemoteCharacteristic *characteristic, uint8_t *, size_t, bool isNotify)
  {
    if (!isNotify)
    {
      return;
    }
    if (notifyQueueCount < maxNotifyQueue)
    {
      notifyQueueCount++;
      fromRadioPending = true;
      bleLogf("FromNum notify queued: %d", notifyQueueCount);
    }
    else
    {
      bleLog("FromNum notify dropped (queue full)");
    }
  };

  if (!fromNum->subscribe(true, notifyCallback, true))
  {
    bleLog("FromNum subscribe failed");
  }
}

void sendMeshtasticNotification(const char *message)
{
  if (!toRadio)
  {
    bleLog("Cannot send notification: toRadio not set");
    return;
  }

  meshtastic_ToRadio req = meshtastic_ToRadio_init_zero;
  req.which_payload_variant = meshtastic_ToRadio_packet_tag;

  meshtastic_MeshPacket *p = &req.packet;
  p->which_payload_variant = meshtastic_MeshPacket_decoded_tag;
  p->to = 0xFFFFFFFF; // Broadcast

  static uint32_t packetId = 0;
  if (packetId == 0)
    packetId = millis();
  p->id = ++packetId;

  int channelIndex = 0;

  bleLogf("Available channels: %d", channelIndices.size());
  for (auto const &[name, index] : channelIndices)
  {
    bleLogf(" - %s: %d", name.c_str(), index);
  }

  if (channelIndices.count("bontastic"))
  {
    channelIndex = channelIndices["bontastic"];
  }
  else if (channelIndices.count("Bontastic"))
  {
    channelIndex = channelIndices["Bontastic"];
  }

  bleLogf("Selected channel index: %d", channelIndex);
  p->channel = channelIndex;

  meshtastic_Data *d = &p->decoded;
  d->portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
  d->payload.size = strlen(message);
  if (d->payload.size > sizeof(d->payload.bytes))
  {
    d->payload.size = sizeof(d->payload.bytes);
  }
  memcpy(d->payload.bytes, message, d->payload.size);

  uint8_t buffer[256];
  pb_ostream_t ostream = pb_ostream_from_buffer(buffer, sizeof(buffer));
  if (!pb_encode(&ostream, meshtastic_ToRadio_fields, &req))
  {
    bleLog("Notification encode failed");
    return;
  }

  bleLogf("Sending notification: %s", message);
  if (!toRadio->writeValue(buffer, ostream.bytes_written, true))
  {
    bleLog("Notification send failed");
  }
}

void loop()
{
  printerControlLoop();

#ifdef ENABLE_CONTEST_QR_MODULE
  contestQrLoop();
#endif

  if (fromRadioPending)
  {
    fromRadioPending = false;
    int count = notifyQueueCount;
    notifyQueueCount = 0;
    bleLogf("Draining FromRadio for %d notify events", count);
    drainFromRadio();
  }
}