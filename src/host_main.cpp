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
  cfg.captivePort = 18081;
  cfg.settingsSave = [](const CamSettings&) { return true; };  // the test checks validation; restarting is the board's job
  cfg.addressJson = [] { return std::string("{\"type\":\"WiFi\",\"dhcp\":true,\"staticFor\":\"\",\"ip\":\"127.0.0.1\",\"mask\":\"255.0.0.0\",\"gateway\":\"127.0.0.1\",\"dns\":\"127.0.0.1\",\"mac\":\"02:00:00:00:00:01\"}\n"); };
  cfg.addressSave = [](const CamAddress&) { return true; };
  cfg.hostName = "hub.local";
  cfg.setupAddress = "127.0.0.1:18080";
  cfg.setupActive = [] { return setupOn; };
  cfg.wifiNetworks = [] { return std::string("{\"name\":\"hub.local\",\"current\":\"\",\"scanning\":false,\"networks\":[{\"ssid\":\"test-net\",\"rssi\":-40,\"open\":false}]}\n"); };
  cfg.wifiSave = [](const std::string& ssid, const std::string& password, bool backup) {
    if (ssid == "setup-on") setupOn = true;
    if (ssid == "setup-off") setupOn = false;
    if (ssid == "want-backup") return backup;
    return password == "p%ss w+rd&ok";
  };
  cfg.rtspPort = 18554;
  cfg.discoveryPort = 13702;
  cfg.serial = "0123456789ab";
  cfg.holdMs = (uint32_t)atol(argv[1]);
  cfg.fsRoot = argv[2];
  if (argc > 3) cfg.webPassword = argv[3];
  SensorCamServer server(cfg, [] { return std::string("127.0.0.1"); }, nowMs);
  if (!server.begin()) {
    fprintf(stderr, "could not bind test ports\n");
    return 1;
  }
  server.networkUp();
  printf("ready\n");
  fflush(stdout);
  for (;;) server.poll(20);
}
