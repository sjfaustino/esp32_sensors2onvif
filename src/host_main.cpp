// PC build of the firmware library for tools/e2e_test.py. Binds to loopback only.
// Usage: program HOLD_MS STORAGE_FOLDER [WEB_PASSWORD [SILENT_MS]]
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include "cam_server.h"

static const char ESPNOW_KEY[] = "test-espnow-key-0123456789";

// Sends a report the way a sensor node would: version 2 signs it, version 1 does not.
static void radio(SensorCamServer* hub, uint8_t last, uint32_t counter, uint8_t battery, int8_t rssi, int version, const char* key, bool active = false) {
  uint8_t mac[6] = {2, 0, 0, 0, 0, last};
  uint8_t p[13 + 8] = {(uint8_t)version, 1, (uint8_t)active, battery, (uint8_t)counter, (uint8_t)(counter >> 8), (uint8_t)(counter >> 16), (uint8_t)(counter >> 24), 0, 0, 0, 0, 0};
  if (version == 2) memcpy(p + 13, camPacketSignature(mac, p, 13, key).data(), 8);
  hub->espNowPacket(mac, p, version == 2 ? 21 : 13, rssi);
}

static bool setupOn = false;

// The test's controls, which only this build has: one command per line on standard input, each answered with
// "done COMMAND" on standard output once it has taken effect, or "unknown COMMAND".
static bool control(SensorCamServer& hub, const std::string& command) {
  static uint32_t counter = 77;
  if (command == "setup-on" || command == "setup-off") {  // stands in for the board opening and closing its setup network
    setupOn = command == "setup-on";
  } else if (command == "end-alerts") {  // so the test need not wait out each hold
    hub.endAlerts();
  } else if (command == "auth-on" || command == "auth-off") {
    hub.setOnvifPassword(command == "auth-on" ? "onvif-secret" : "");
  } else if (command.compare(0, 6, "radio-") == 0) {  // this build has no radio, so the test delivers reports this way
    uint8_t node = command == "radio-unknown" ? 0x99 : 1;
    if (command == "radio-huge") radio(&hub, node, 2147483700UL, 55, -61, 2, ESPNOW_KEY);  // above the largest signed 32-bit number
    else if (command == "radio-unsigned") radio(&hub, node, counter++, 55, -61, 1, "");
    else if (command == "radio-forged") radio(&hub, node, counter++, 55, -61, 2, "not-the-key");
    else if (command == "radio-old") radio(&hub, node, counter - 10, 55, -61, 2, ESPNOW_KEY);
    else if (command == "radio-restart-alert") radio(&hub, node, 1, 55, -61, 2, ESPNOW_KEY, true);  // a sensor that restarted, firing
    else if (command == "radio-weak")
      for (int i = 0; i < 10; i++) radio(&hub, node, counter++, 55, -90, 2, ESPNOW_KEY);
    else if (command == "radio-known" || command == "radio-unknown" || command == "radio-low")
      radio(&hub, node, counter++, command == "radio-low" ? 10 : 55, -61, 2, ESPNOW_KEY);
    else return false;
  } else {
    return false;
  }
  return true;
}

// Commands arrive on another thread, so the hub itself is only ever used from the main loop.
static std::mutex commandLock;
static std::deque<std::string> commands;

static void readCommands() {
  char line[256];
  while (fgets(line, sizeof(line), stdin)) {
    std::string command = line;
    while (!command.empty() && (command.back() == '\n' || command.back() == '\r')) command.pop_back();
    std::lock_guard<std::mutex> hold(commandLock);
    commands.push_back(command);
  }
}

static uint32_t nowMs() {
  using namespace std::chrono;
  return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: program HOLD_MS STORAGE_FOLDER [WEB_PASSWORD [SILENT_MS]]\n");
    return 1;
  }
  CamConfig cfg;
  cfg.loopbackOnly = true;
  cfg.onvifPort = 18899;
  cfg.webPort = 18080;
  cfg.captivePort = 18081;
  cfg.settingsSave = [](const CamSettings&) { return true; };  // the test checks validation; restarting is the board's job
  cfg.addressJson = [] { return std::string("{\"type\":\"WiFi\",\"dhcp\":true,\"staticFor\":\"\",\"ip\":\"127.0.0.1\",\"mask\":\"255.0.0.0\",\"gateway\":\"127.0.0.1\",\"dns\":\"127.0.0.1\",\"mac\":\"02:00:00:00:00:01\"}\n"); };
  cfg.addressSave = [](const CamAddress&) { return true; };
  static bool firmwareGood = false;
  cfg.firmwareBegin = [](size_t) {
    firmwareGood = false;
    return true;
  };
  cfg.firmwareWrite = [](const uint8_t* data, size_t len) {
    static bool first = true;
    if (first || !firmwareGood) firmwareGood = len && data[0] == 0xE9;
    first = false;
    return firmwareGood;
  };
  cfg.firmwareEnd = [](bool commit) { return !commit || firmwareGood; };
  cfg.backupText = [] { return std::string("sensorcam-backup 1\nnet.name=%68%75%62\n"); };
  cfg.restoreText = [](const std::string& text) { return text.compare(0, 19, "sensorcam-backup 1\n") == 0; };
  cfg.healthSave = [](bool) { return true; };
  cfg.nvrSave =[](const std::string&) { return true; };
  cfg.timeJson = [] { return std::string("{\"ntp\":true,\"server\":\"pool.ntp.org\",\"offset\":0,\"dst\":0,\"synced\":true,\"local\":\"2026-01-01 00:00:00\"}\n"); };
  cfg.timeSave = [](const CamTime&) { return true; };
  cfg.scheduleSave = [](const CamSchedule&) { return true; };
  cfg.deviceRows = [] { return std::string("[\"Firmware version\",\"test\"]"); };
  // A pretend 3000-byte crash report, byte i being i % 251, so the test can check a download read in pieces.
  static size_t crashLeft = 3000;
  cfg.crashSize = [] { return crashLeft; };
  cfg.crashRead = [](size_t offset, char* out, size_t len) {
    for (size_t i = 0; i < len; i++) out[i] = (char)((offset + i) % 251);
    return offset + len <= crashLeft;
  };
  cfg.crashDelete = [] { crashLeft = 0; return true; };
  cfg.maintenance = [](const std::string&) { return true; };  // the test checks the requests; restarting is the board's job
  cfg.espNowKey = ESPNOW_KEY;
  cfg.espNowSave = [](bool) { return true; };
  cfg.webhookSave = [](const CamWebhook&) { return true; };
  cfg.localMinute = [] { return (int)(time(nullptr) / 60 % 1440); };  // UTC, which the test also uses
  cfg.hostName = "hub.local";
  cfg.setupAddress = "127.0.0.1:18080";
  cfg.setupActive = [] { return setupOn; };
  cfg.wifiNetworks = [] { return std::string("{\"name\":\"hub.local\",\"current\":\"\",\"scanning\":false,\"networks\":[{\"ssid\":\"test-net\",\"rssi\":-40,\"open\":false}]}\n"); };
  // Checks what the WiFi form passes on: the backup flag, and the password decoded exactly.
  cfg.wifiSave = [](const std::string& ssid, const std::string& password, bool backup) {
    if (ssid == "want-backup") return backup;
    return password == "p%ss w+rd&ok";
  };
  cfg.rtspPort = 18554;
  cfg.discoveryPort = 13702;
  cfg.serial = "0123456789ab";
  cfg.holdMs = (uint32_t)atol(argv[1]);
  cfg.fsRoot = argv[2];
  if (argc > 3) cfg.webPassword = argv[3];
  if (argc > 4) cfg.silentMs = (uint32_t)atol(argv[4]);
  SensorCamServer server(cfg, [] { return std::string("127.0.0.1"); }, nowMs);
  if (!server.begin()) {
    fprintf(stderr, "could not bind test ports\n");
    return 1;
  }
  server.networkUp();
  std::thread(readCommands).detach();
  printf("ready\n");
  fflush(stdout);
  for (;;) {
    server.poll(20);
    std::deque<std::string> waiting;
    {
      std::lock_guard<std::mutex> hold(commandLock);
      waiting.swap(commands);
    }
    for (const std::string& command : waiting) {
      printf("%s %s\n", control(server, command) ? "done" : "unknown", command.c_str());
      fflush(stdout);
    }
  }
}
