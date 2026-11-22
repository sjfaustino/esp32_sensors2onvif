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
#include <random>
#include <string>
#include <thread>
#include "cam_server.h"
#ifdef HOST_COVERAGE
extern "C" void __gcov_dump(void);
extern "C" void __gcov_reset(void);
#endif

static const char ESPNOW_KEY[] = "test-espnow-key-0123456789";

static bool setupOn = false;
static bool firmwareRefused = false;            // the next update cannot start, as on a board short of memory
static uint32_t diskTotal = 10u * 1024 * 1024;  // a pretend storage size, which the test can shrink
static std::string lastSent;                    // the last ESP-NOW message the hub sent, as hex: this build has no radio to send it on
static std::string allSent;                     // every one since "all-sent" was last asked, each as lastSent, joined by ";"

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

// Sends a report the way a sensor node would, signed with key.
static void radio(SensorCamServer* hub, uint8_t last, uint32_t counter, uint8_t battery, int8_t rssi, const char* key, bool active = false) {
  uint8_t mac[6] = {2, 0, 0, 0, 0, last};
  SensorReport r;
  r.active = active;
  r.battery = battery;
  r.counter = counter;
  uint8_t p[S2O_REPORT_MAX];
  int len = s2oReportBody(r, p);
  memcpy(p + len, camPacketSignature(mac, p, len, key).data(), 8);
  HubCall call;
  hub->espNowPacket(mac, p, len + 8, rssi);
}

// The test's controls, which only this build has: one command per line on standard input, each answered with
// "done COMMAND" on standard output once it has taken effect, or "unknown COMMAND". A command with an answer gives it
// after a colon: "done COMMAND: ANSWER".
static bool control(SensorCamServer& hub, const std::string& command, std::string& reply) {
  static uint32_t counter = 77;
  if (command == "setup-on" || command == "setup-off") {  // stands in for the board opening and closing its setup network
    setupOn = command == "setup-on";
  } else if (command == "end-alerts") {  // so the test need not wait out each hold
    hub.alarm().endAlerts();
  } else if (command == "forget-waiting") {  // so a test need not wait out the past events another left
    hub.alarm().forgetWaiting();
  } else if (command == "routes") {  // every route of the API, and who may use it
    reply = hub.routes();
  } else if (command == "last-sent") {  // answered "done last-sent: MAC HEX", or "done last-sent" when nothing was sent
    reply = lastSent;
    lastSent.clear();
  } else if (command == "all-sent") {
    reply = allSent;
    allSent.clear();
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
  } else if (command == "stats-fail" || command == "stats-work") {  // listed entries cannot be looked at, as when memory runs out
    camStorageStatsFail = command == "stats-fail";
  } else if (command == "save-now") {  // what the board does before a planned restart
    hub.saveNow();
  } else if (command.compare(0, 12, "busy-alerts ") == 0) {  // how many alerts within an hour count as firing often
    hub.alarm().setBusyAlerts((int)strtol(command.c_str() + 12, nullptr, 10));
  } else if (command.compare(0, 11, "overdue-ms ") == 0) {  // how late a node's report is overdue; 0 for from its report time
    hub.alarm().setOverdueMs((uint32_t)strtoul(command.c_str() + 11, nullptr, 10));
  } else if (command == "minutes-fast") {  // a minute of a timed disarm lasts a second, so the test need not wait
    hub.alarm().setMinuteMs(1000);
  } else if (command == "network-lost") {  // as the board reports rejoining Wi-Fi after two minutes off it
    hub.networkLost(125);
  } else if (command == "drain-fast" || command == "drain-normal") {  // so the test need not trickle a body for a minute
    hub.setDrainMs(command == "drain-fast" ? 3000 : CAM_HTTP_DRAIN_MS);
  } else if (command == "nvr-let-in") {  // how often the NVR took the place of another connection
    reply = std::to_string(hub.nvrLetIn());
  } else if (command == "nvr-video-let-in") {  // and of another video connection
    reply = std::to_string(hub.nvrVideoLetIn());
  } else if (command == "nvr-gone-fast" || command == "nvr-gone-normal") {  // so the test need not wait 3 minutes
    hub.alarm().setNvrGoneMs(command == "nvr-gone-fast" ? 3000 : 3 * 60 * 1000);
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
    if (command == "radio-huge")
      radio(&hub, node, 2147483700UL, 55, -61, ESPNOW_KEY);  // above the largest signed 32-bit number
    else if (command == "radio-forged")
      radio(&hub, node, counter++, 55, -61, "not-the-key");
    else if (command == "radio-old")
      radio(&hub, node, counter - 10, 55, -61, ESPNOW_KEY);
    else if (command == "radio-alert")
      radio(&hub, node, counter++, 55, -61, ESPNOW_KEY, true);
    else if (command == "radio-restart-alert")
      radio(&hub, node, 1, 55, -61, ESPNOW_KEY, true);  // a sensor that restarted, firing
    else if (command == "radio-weak")
      for (int i = 0; i < 10; i++) radio(&hub, node, counter++, 55, -90, ESPNOW_KEY);
    else if (command == "radio-known" || command == "radio-unknown" || command == "radio-low")
      radio(&hub, node, counter++, command == "radio-low" ? 10 : 55, -61, ESPNOW_KEY);
    else
      return false;
  } else {
    return false;
  }
  return true;
}

// Commands arrive on another thread, so the hub itself is only ever used from the main loop.
static std::mutex commandLock;
static std::deque<std::string> commands;  // NOLINT(bugprone-throwing-static-initialization): the test build only

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

// ---- the PC build's pretend board, for tools/e2e_test.py ----

static std::string storageFolder;

class TestChip : public CamChip {
 public:
  bool random32(uint32_t& out) override {
    static std::random_device source;
    out = source();
    return true;
  }
  bool storageUsage(uint32_t& used, uint32_t& total) override {  // what is really stored, of a pretend storage size
    std::function<uint32_t(const CamStorage&, const std::string&)> sum = [&](const CamStorage& s, const std::string& path) {
      std::vector<CamFsEntry> items;
      s.list(path, items);
      uint32_t bytes = 0;
      for (const CamFsEntry& e : items) bytes += e.dir ? sum(s, path.empty() ? e.name : path + "/" + e.name) : (e.size + 4095) / 4096 * 4096;
      return bytes;
    };
    used = sum(CamStorage(storageFolder), "");
    total = diskTotal > used ? diskTotal : used;
    return true;
  }
  bool firmwareBegin(size_t) override {
    firmwareGood = false;
    return !firmwareRefused;
  }
  bool firmwareWrite(const uint8_t* data, size_t len) override {
    if (first || !firmwareGood) firmwareGood = len && data[0] == 0xE9;
    first = false;
    return firmwareGood;
  }
  bool firmwareEnd(bool commit) override { return !commit || firmwareGood; }
  // A pretend 3000-byte crash report, byte i being i % 251, so the test can check a download read in pieces.
  size_t crashSize() override { return crashLeft; }
  bool crashRead(size_t offset, char* out, size_t len) override {
    for (size_t i = 0; i < len; i++) out[i] = (char)((offset + i) % 251);
    return offset + len <= crashLeft;
  }
  bool crashDelete() override {
    crashLeft = 0;
    return true;
  }
  bool maintenance(const std::string&) override { return true; }  // the test checks the requests; restarting is the board's job
  void deviceRows(CamJson& j) override { j.array().str("Firmware version").str("test").end(); }
  bool tellsHealth() override { return true; }
  void health(CamHealth& h) override {  // pretend figures, so the test can check they arrive
    h.freeHeap = 140 * 1024;
    h.lowestFreeHeap = 120 * 1024;
    h.largestBlock = 76 * 1024;
    h.worstLargestBlock = 60 * 1024;
    h.wifiRssi = -58;
    h.uptimeS = 3600;
  }
  int localMinute() override { return (int)(time(nullptr) / 60 % 1440); }         // UTC, which the test also uses
  int localWeekday() override { return (int)((time(nullptr) / 86400 + 4) % 7); }  // 1 January 1970 was a Thursday
  void timeInfo(CamJson& j) override {
    j.flag("ntp", true).str("server", "pool.ntp.org").num("offset", 0).num("dst", 0).flag("synced", true).str("local", "2026-01-01 00:00:00");
  }
  bool timeSave(const CamTime&) override { return true; }

 private:
  bool firmwareGood = false, first = true;
  size_t crashLeft = 3000;
};

class TestRadio : public CamRadio {
 public:
  bool setupActive() override { return setupOn; }
  void wifiNetworks(CamJson& j) override {  // a network name can hold any character, so the second one tests the escaping
    j.str("name", "hub.local").str("current", "").str("backup", "").flag("scanning", false).array("networks");
    j.object().str("ssid", "test-net").num("rssi", -40).flag("open", false).end();
    j.object().str("ssid", "say \"hi\" \\ to\tall\x01").num("rssi", -70).flag("open", true).end();
    j.end();
  }
  // Checks what the WiFi form passes on: the backup flag, and the password decoded exactly.
  bool wifiSave(const std::string& ssid, const std::string& password, bool backup) override {
    if (ssid == "want-backup") return backup;
    return password == "p%ss w+rd&ok";
  }
  void addressInfo(CamJson& j) override {
    j.str("type", "WiFi").flag("dhcp", true).str("staticFor", "").str("ip", "127.0.0.1").str("mask", "255.0.0.0").str("gateway", "127.0.0.1");
    j.str("dns", "127.0.0.1").str("mac", "02:00:00:00:00:01");
  }
  bool addressSave(const CamAddress&) override { return true; }
  int wifiChannel() override { return 6; }
  bool hasEspNow() override { return true; }
  void espNowSend(const uint8_t mac[6], const uint8_t* data, int len) override {
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
    if (allSent.size() < 400000) allSent += (allSent.empty() ? "" : ";") + lastSent;
  }
};

// Saving always works here: the test checks validation, and keeping settings is the board's job.
class TestStore : public CamStore {
 public:
  bool settingsSave(const CamSettings&) override { return true; }
  bool passwordSave(const std::string& password) override { return password == "first-password"; }
  bool healthSave(bool) override { return true; }
  std::string backupText() override { return std::string("sensorcam-backup 1\nnet.name=%68%75%62\n"); }
  bool restoreText(const std::string& text) override { return text.compare(0, 19, "sensorcam-backup 1\n") == 0; }
  bool scheduleSave(const CamSchedule&) override { return true; }
  bool webhookSave(const CamWebhook&) override { return true; }
  bool armedSave(CamArmMode, int64_t) override { return true; }
  bool rearmSave(uint32_t) override { return true; }
  bool mqttSave(const CamMqtt&) override { return true; }
  bool nvrSave(const std::string&) override { return true; }
  bool sealReserve(uint32_t) override { return true; }
};

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
  cfg.espNowKey = ESPNOW_KEY;
  cfg.pairingKey = "a8abababababababababababababababababababababababababababababab6b";  // fixed, so the test knows the hub's key
  cfg.startReason = "PC build started";
  cfg.snapshotSecret = "test-snapshot-secret";
  cfg.sealStart = 0x5EA10000;
  storageFolder = argv[2];
  cfg.firmwareVersion = "test";
  // The public half of test/firmware-test-key.pem, which signs the test's updates; never the release key.
  cfg.firmwareKey = "dfc5cbdf9e1c75ed681052835e1c02e0bb2f93d37916b9b9b445764120d03407";
  cfg.firmwareRelease = 4000;  // as if release 0.4.0; the test sends older and newer ones
  cfg.hostName = "hub.local";
  cfg.setupAddress = "127.0.0.1:18080";
  static TestChip chip;
  static TestRadio radio;
  static TestStore store;
  cfg.chip = &chip;
  cfg.radio = &radio;
  cfg.store = &store;
  cfg.rtspPort = 18554;
  cfg.discoveryPort = 13702;
  cfg.serial = "0123456789ab";
  cfg.holdMs = (uint32_t)strtoul(argv[1], nullptr, 10);
  cfg.fsRoot = argv[2];
  if (argc > 3) cfg.webPassword = argv[3];
  if (argc > 4) cfg.silentMs = (uint32_t)strtoul(argv[4], nullptr, 10);
  SensorCamServer server(cfg, [] { return std::string("127.0.0.1"); }, nowMs);
  server.alarm().setBusyAlerts(100000);  // the tests fire sensors far more often than any real one; busy-alerts sets it
  if (!server.begin()) {
    fprintf(stderr, "could not bind test ports\n");
    return 1;
  }
  server.networkUp();
  std::thread(readCommands).detach();
  printf("ready\n");
  (void)fflush(stdout);
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
      (void)fflush(stdout);
    }
#ifdef HOST_COVERAGE
    // The test stops this program by killing it, which writes no coverage, so the counts are written every second
    // instead, and started again from zero, so each write adds only what ran since (tools/coverage.py).
    static uint32_t lastDump = nowMs();
    if (nowMs() - lastDump >= 1000) {
      lastDump = nowMs();
      __gcov_dump();
      __gcov_reset();
    }
#endif
  }
}
