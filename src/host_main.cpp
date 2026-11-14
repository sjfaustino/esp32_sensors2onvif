// PC build of the firmware library for tools/e2e_test.py. Binds to loopback only.
// Usage: program HOLD_MS STORAGE_FOLDER [WEB_PASSWORD [SILENT_MS]]
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include "cam_server.h"

static const char ESPNOW_KEY[] = "test-espnow-key-0123456789";

static bool setupOn = false;
static bool firmwareRefused = false;  // the next update cannot start, as on a board short of memory
static uint32_t diskTotal = 10u * 1024 * 1024;  // a pretend storage size, which the test can shrink
static std::string lastSent;  // the last ESP-NOW message the hub sent, as hex: this build has no radio to send it on

// As on the board when memory is short and broken up: a request for a block larger than this fails, as the board's does.
static std::atomic<size_t> largestBlock{0};  // 0 for no limit
// As on a board with no memory left at all: every request the hub makes fails. Only the hub's own calls, so this
// program can still read the test's commands.
static std::atomic<bool> memoryGone{false};
static thread_local bool inHub = false;
struct HubCall {
  HubCall() { inHub = true; }
  ~HubCall() { inHub = false; }
};
void* operator new(size_t n) {
  size_t limit = largestBlock.load();
  void* p = (limit && n > limit) || (inHub && memoryGone.load()) ? nullptr : malloc(n ? n : 1);
  if (!p) throw std::bad_alloc();
  return p;
}
void operator delete(void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }

// Sends a report the way a sensor node would: version 2 signs it, version 1 does not.
static void radio(SensorCamServer* hub, uint8_t last, uint32_t counter, uint8_t battery, int8_t rssi, int version, const char* key, bool active = false) {
  uint8_t mac[6] = {2, 0, 0, 0, 0, last};
  uint8_t p[13 + 8] = {(uint8_t)version, 1, (uint8_t)active, battery, (uint8_t)counter, (uint8_t)(counter >> 8), (uint8_t)(counter >> 16), (uint8_t)(counter >> 24), 0, 0, 0, 0, 0};
  if (version == 2) memcpy(p + 13, camPacketSignature(mac, p, 13, key).data(), 8);
  HubCall call;
  hub->espNowPacket(mac, p, version == 2 ? 21 : 13, rssi);
}

// The test's controls, which only this build has: one command per line on standard input, each answered with
// "done COMMAND" on standard output once it has taken effect, or "unknown COMMAND". A command with an answer gives it
// after a colon: "done COMMAND: ANSWER".
static bool control(SensorCamServer& hub, const std::string& command, std::string& reply) {
  static uint32_t counter = 77;
  if (command == "setup-on" || command == "setup-off") {  // stands in for the board opening and closing its setup network
    setupOn = command == "setup-on";
  } else if (command == "end-alerts") {  // so the test need not wait out each hold
    hub.endAlerts();
  } else if (command == "last-sent") {  // answered "done last-sent: MAC HEX", or "done last-sent" when nothing was sent
    reply = lastSent;
    lastSent.clear();
  } else if (command == "disk-nearly-full" || command == "disk-normal") {  // the storage left over beside what is stored
    diskTotal = command == "disk-normal" ? 10u * 1024 * 1024 : 300u * 1024;
  } else if (command == "firmware-refused" || command == "firmware-allowed") {
    firmwareRefused = command == "firmware-refused";
  } else if (command == "memory-short" || command == "memory-normal") {  // no block above 2 KB, as on a hub short of memory
    largestBlock = command == "memory-short" ? 2048 : 0;
    memoryGone = false;
  } else if (command == "memory-gone") {  // no memory at all; memory-normal ends it
    memoryGone = true;
  } else if (command == "memory-failures") {  // answered with how often the hub dropped work for lack of memory
    reply = std::to_string(hub.memoryFailures());
  } else if (command == "lists-fail" || command == "lists-work") {  // folder listings fail, as on a storage error
    camStorageListsFail = command == "lists-fail";
  } else if (command == "minutes-fast") {  // a minute of a timed disarm lasts a second, so the test need not wait
    hub.setMinuteMs(1000);
  } else if (command == "network-lost") {  // as the board reports rejoining Wi-Fi after two minutes off it
    hub.networkLost(125);
  } else if (command == "nvr-gone-fast" || command == "nvr-gone-normal") {  // so the test need not wait 3 minutes
    hub.setNvrGoneMs(command == "nvr-gone-fast" ? 3000 : 3 * 60 * 1000);
  } else if (command == "auth-on" || command == "auth-off") {
    hub.setOnvifPassword(command == "auth-on" ? "onvif-secret" : "");
  } else if (command.compare(0, 10, "radio-raw ") == 0) {  // "radio-raw MAC DATA", both hex: any message, as the radio would deliver it
    std::string mac = command.substr(10, 12), data = command.size() > 23 ? command.substr(23) : "";
    uint8_t from[6], bytes[250];
    if (data.size() % 2 || data.size() / 2 > sizeof(bytes) || mac.size() != 12) return false;
    for (int i = 0; i < 6; i++) from[i] = (uint8_t)strtoul(mac.substr(2 * i, 2).c_str(), nullptr, 16);
    for (size_t i = 0; i < data.size() / 2; i++) bytes[i] = (uint8_t)strtoul(data.substr(2 * i, 2).c_str(), nullptr, 16);
    HubCall call;
    hub.espNowPacket(from, bytes, (int)data.size() / 2, -55);
  } else if (command.compare(0, 6, "radio-") == 0) {  // this build has no radio, so the test delivers reports this way
    uint8_t node = command == "radio-unknown" ? 0x99 : 1;
    if (command == "radio-huge") radio(&hub, node, 2147483700UL, 55, -61, 2, ESPNOW_KEY);  // above the largest signed 32-bit number
    else if (command == "radio-unsigned") radio(&hub, node, counter++, 55, -61, 1, "");
    else if (command == "radio-forged") radio(&hub, node, counter++, 55, -61, 2, "not-the-key");
    else if (command == "radio-old") radio(&hub, node, counter - 10, 55, -61, 2, ESPNOW_KEY);
    else if (command == "radio-alert") radio(&hub, node, counter++, 55, -61, 2, ESPNOW_KEY, true);
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
  cfg.addressInfo = [](CamJson& j) {
    j.str("type", "WiFi").flag("dhcp", true).str("staticFor", "").str("ip", "127.0.0.1").str("mask", "255.0.0.0").str("gateway", "127.0.0.1");
    j.str("dns", "127.0.0.1").str("mac", "02:00:00:00:00:01");
  };
  cfg.addressSave = [](const CamAddress&) { return true; };
  static bool firmwareGood = false;
  cfg.firmwareBegin = [](size_t) {
    firmwareGood = false;
    return !firmwareRefused;
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
  cfg.timeInfo = [](CamJson& j) { j.flag("ntp", true).str("server", "pool.ntp.org").num("offset", 0).num("dst", 0).flag("synced", true).str("local", "2026-01-01 00:00:00"); };
  cfg.timeSave = [](const CamTime&) { return true; };
  cfg.scheduleSave = [](const CamSchedule&) { return true; };
  cfg.deviceRows = [](CamJson& j) { j.array().str("Firmware version").str("test").end(); };
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
  cfg.pairingKey = "a8abababababababababababababababababababababababababababababab6b";  // fixed, so the test knows the hub's key
  cfg.wifiChannel = [] { return 6; };
  cfg.espNowSend = [](const uint8_t mac[6], const uint8_t* data, int len) {
    char hex[3];
    lastSent.clear();
    for (int i = 0; i < 6; i++) {
      snprintf(hex, sizeof(hex), "%02x", mac[i]);
      lastSent += hex;
    }
    lastSent += ' ';
    for (int i = 0; i < len; i++) {
      snprintf(hex, sizeof(hex), "%02x", data[i]);
      lastSent += hex;
    }
  };
  cfg.startReason = "PC build started";
  cfg.snapshotSecret = "test-snapshot-secret";
  static std::string storageFolder = argv[2];
  cfg.fsUsage = [](uint32_t& used, uint32_t& total) {  // what is really stored, of a pretend storage size
    std::function<uint32_t(const CamStorage&, const std::string&)> sum = [&](const CamStorage& s, const std::string& path) {
      std::vector<CamFsEntry> items;
      s.list(path, items);
      uint32_t bytes = 0;
      for (const CamFsEntry& e : items) bytes += e.dir ? sum(s, path.empty() ? e.name : path + "/" + e.name) : (e.size + 4095) / 4096 * 4096;
      return bytes;
    };
    used = sum(CamStorage(storageFolder), "");
    total = diskTotal > used ? diskTotal : used;
  };
  cfg.espNowSave = [](bool) { return true; };
  cfg.webhookSave = [](const CamWebhook&) { return true; };
  cfg.mqttSave = [](const CamMqtt&) { return true; };
  cfg.armedSave = [](CamArmMode, int64_t) { return true; };
  cfg.rearmSave = [](uint32_t) { return true; };
  cfg.firmwareVersion = "test";
  cfg.healthInfo = [](CamHealth& h) {  // pretend figures, so the test can check they arrive
    h.freeHeap = 140 * 1024;
    h.lowestFreeHeap = 120 * 1024;
    h.largestBlock = 76 * 1024;
    h.worstLargestBlock = 60 * 1024;
    h.wifiRssi = -58;
    h.uptimeS = 3600;
  };
  cfg.localMinute = [] { return (int)(time(nullptr) / 60 % 1440); };  // UTC, which the test also uses
  cfg.hostName = "hub.local";
  cfg.setupAddress = "127.0.0.1:18080";
  cfg.setupActive = [] { return setupOn; };
  cfg.wifiNetworks = [](CamJson& j) {  // a network name can hold any character, so the second one tests the escaping
    j.str("name", "hub.local").str("current", "").str("backup", "").flag("scanning", false).array("networks");
    j.object().str("ssid", "test-net").num("rssi", -40).flag("open", false).end();
    j.object().str("ssid", "say \"hi\" \\ to\tall\x01").num("rssi", -70).flag("open", true).end();
    j.end();
  };
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
    {
      HubCall call;
      server.poll(20);
    }
    std::deque<std::string> waiting;
    {
      std::lock_guard<std::mutex> hold(commandLock);
      waiting.swap(commands);
    }
    for (const std::string& command : waiting) {
      std::string reply;
      bool known = control(server, command, reply);
      printf("%s %s%s\n", known ? "done" : "unknown", command.c_str(), reply.empty() ? "" : (": " + reply).c_str());
      fflush(stdout);
    }
  }
}
