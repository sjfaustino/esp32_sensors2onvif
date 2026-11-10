// PC build of the firmware library for tools/e2e_test.py. Binds to loopback only.
// Usage: program HOLD_MS STORAGE_FOLDER [WEB_PASSWORD]
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include "cam_server.h"

static uint32_t nowMs() {
  using namespace std::chrono;
  return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: program HOLD_MS STORAGE_FOLDER [WEB_PASSWORD]\n");
    return 1;
  }
  CamConfig cfg;
  cfg.loopbackOnly = true;
  cfg.onvifPort = 18899;
  cfg.webPort = 18080;
  // Stand-ins for the board's Wi-Fi. The test opens and closes setup mode by saving these two names.
  static bool setupOn = false;
  static SensorCamServer* hub = nullptr;
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
  cfg.maintenance = [](const std::string&) { return true; };  // the test checks the requests; restarting is the board's job
  cfg.hostName = "hub.local";
  cfg.setupAddress = "127.0.0.1:18080";
  cfg.setupActive = [] { return setupOn; };
  cfg.wifiNetworks = [] { return std::string("{\"name\":\"hub.local\",\"current\":\"\",\"scanning\":false,\"networks\":[{\"ssid\":\"test-net\",\"rssi\":-40,\"open\":false}]}\n"); };
  cfg.wifiSave = [](const std::string& ssid, const std::string& password, bool backup) {
    if (ssid == "setup-on") setupOn = true;
    if (ssid == "setup-off") setupOn = false;
    if (ssid == "want-backup") return backup;
    if (ssid == "auth-on" || ssid == "auth-off") {
      hub->setOnvifPassword(ssid == "auth-on" ? "onvif-secret" : "");
      return true;
    }
    if (ssid == "radio-known" || ssid == "radio-unknown" || ssid == "radio-low") {  // this build has no radio, so the test delivers a report this way
      bool known = ssid != "radio-unknown";
      uint8_t mac[6] = {2, 0, 0, 0, 0, (uint8_t)(known ? 1 : 0x99)};
      static uint32_t counter = 77;
      SensorReport report;
      report.counter = counter++;
      report.battery = ssid == "radio-low" ? 10 : 55;
      hub->espNowInput(mac, report, -61);
      return true;
    }
    return password == "p%ss w+rd&ok";
  };
  cfg.rtspPort = 18554;
  cfg.discoveryPort = 13702;
  cfg.serial = "0123456789ab";
  cfg.holdMs = (uint32_t)atol(argv[1]);
  cfg.fsRoot = argv[2];
  if (argc > 3) cfg.webPassword = argv[3];
  SensorCamServer server(cfg, [] { return std::string("127.0.0.1"); }, nowMs);
  hub = &server;
  if (!server.begin()) {
    fprintf(stderr, "could not bind test ports\n");
    return 1;
  }
  server.networkUp();
  printf("ready\n");
  fflush(stdout);
  for (;;) server.poll(20);
}
