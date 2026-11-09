#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_now.h>
#include <freertos/queue.h>
#include <ping/ping_sock.h>
#include "cam_server.h"
#include "config.h"

static std::string localIp() { return WiFi.localIP().toString().c_str(); }
static uint32_t nowMs() { return millis(); }

static std::string macSerial() {
  uint64_t mac = ESP.getEfuseMac();
  char b[13];
  snprintf(b, sizeof(b), "%012llx", (unsigned long long)mac);
  return b;
}

// ---- Wi-Fi settings and the setup network ----
static const uint32_t WIFI_TRY_MS = 30000;  // how long each saved network gets before moving on
static String wifiSsid[2], wifiPassword[2];  // [0] is the main network, [1] the backup
static int wifiTrying = 0;
static uint32_t wifiTryAt = 0;
static DNSServer dnsServer;
static bool setupOpen = false;
static bool wifiSaved = false;
static uint32_t wifiSavedAt = 0;
static uint32_t savedCause = 5;
static uint16_t onvifPort = ONVIF_PORT, webPort = WEB_PORT, rtspPort = RTSP_PORT;
static String hubName = MDNS_NAME, webPassword = WEB_PASSWORD;
// A fixed address belongs to one WiFi network, because the main and backup networks can use different address ranges.
static String staticFor, staticIp, staticMask, staticGateway, staticDns;
static bool usingStatic = false;
static std::string nvrIp;
static uint32_t connectedAt = 0;
static const uint32_t STATIC_CHECK_MS = 120000;

// Settings saved on the web page win over the defaults compiled in from config.h and secrets.h.
static void loadSettings() {
  Preferences p;
  if (!p.begin("net", false)) return;
  onvifPort = p.getUShort("onvif", ONVIF_PORT);
  webPort = p.getUShort("web", WEB_PORT);
  rtspPort = p.getUShort("rtsp", RTSP_PORT);
  hubName = p.getString("name", MDNS_NAME);
  webPassword = p.getString("webpw", WEB_PASSWORD);
  nvrIp = p.getString("nvrip", "").c_str();
  staticFor = p.getString("ipfor", "");
  staticIp = p.getString("ip", "");
  staticMask = p.getString("mask", "");
  staticGateway = p.getString("gw", "");
  staticDns = p.getString("dns", "");
  p.end();
}

static bool saveSettings(const CamSettings& s) {
  Preferences p;
  if (!p.begin("net", false)) return false;
  bool ok = p.putUShort("onvif", s.onvifPort) == 2 && p.putUShort("web", s.webPort) == 2 && p.putUShort("rtsp", s.rtspPort) == 2;
  if (!s.name.empty()) ok = ok && p.putString("name", s.name.c_str()) == s.name.size();
  if (!s.passwordOn) p.putString("webpw", "");
  else if (!s.password.empty()) ok = ok && p.putString("webpw", s.password.c_str()) == s.password.size();
  p.end();
  if (ok) {
    savedCause = 6;
    wifiSavedAt = millis();
    wifiSaved = true;
  }
  return ok;
}

// Keeps the name and password, which cannot stop the ports from opening.
static void forgetPorts() {
  Preferences p;
  if (p.begin("net", false)) {
    p.remove("onvif");
    p.remove("web");
    p.remove("rtsp");
    p.end();
  }
}

// Networks saved on the setup page win over the one compiled in from secrets.h.
static void loadWifi() {
  Preferences p;
  wifiSsid[0] = WIFI_SSID;
  wifiPassword[0] = WIFI_PASSWORD;
  if (p.begin("wifi", false)) {
    wifiSsid[0] = p.getString("ssid", wifiSsid[0]);
    wifiPassword[0] = p.getString("password", wifiPassword[0]);
    wifiSsid[1] = p.getString("ssid2", "");
    wifiPassword[1] = p.getString("password2", "");
    p.end();
  }
}

static bool storeWifi() {
  Preferences p;
  if (!p.begin("wifi", false)) return false;
  bool ok = p.putString("ssid", wifiSsid[0]) == wifiSsid[0].length() && p.putString("password", wifiPassword[0]) == wifiPassword[0].length() &&
            p.putString("ssid2", wifiSsid[1]) == wifiSsid[1].length() && p.putString("password2", wifiPassword[1]) == wifiPassword[1].length();
  p.end();
  return ok;
}

static bool saveWifi(const std::string& ssid, const std::string& password, bool backup) {
  String name = ssid.c_str(), pass = password.c_str();
  if (backup) {
    wifiSsid[1] = name;
    wifiPassword[1] = pass;
  } else {
    if (name != wifiSsid[0]) {  // the network it replaces is kept as the backup
      wifiSsid[1] = wifiSsid[0];
      wifiPassword[1] = wifiPassword[0];
    }
    wifiSsid[0] = name;
    wifiPassword[0] = pass;
  }
  if (!storeWifi()) return false;
  if (!backup || WiFi.status() != WL_CONNECTED) {  // a new backup needs no restart while the main network is working
    wifiSavedAt = millis();
    wifiSaved = true;  // the restart waits for the reply to reach the browser
  }
  return true;
}

static void tryNetwork(int i) {
  wifiTrying = i;
  wifiTryAt = millis();
  WiFi.disconnect(false);
  IPAddress ip, mask, gateway, dns;
  usingStatic = staticFor.length() && staticFor == wifiSsid[i] && ip.fromString(staticIp) && mask.fromString(staticMask) && gateway.fromString(staticGateway);
  if (usingStatic) WiFi.config(ip, gateway, mask, dns.fromString(staticDns) ? dns : gateway);
  else WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.begin(wifiSsid[i].c_str(), wifiPassword[i].c_str());
}

// The backup worked, so it becomes the main network and the old main becomes the backup.
static void promoteBackup() {
  std::swap(wifiSsid[0], wifiSsid[1]);
  std::swap(wifiPassword[0], wifiPassword[1]);
  wifiTrying = 0;
  bool ok = storeWifi();
  Serial.printf("Joined the backup network; it is now the main network%s.\n", ok ? "" : " until the next restart (could not save)");
}

static void openSetup() {
  if (setupOpen || strlen(SETUP_AP_PASSWORD) < 8) return;
  WiFi.disconnect(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(SETUP_AP_NAME, SETUP_AP_PASSWORD);
  dnsServer.start(53, "*", WiFi.softAPIP());
  setupOpen = true;
  Serial.printf("Setup network %s is open at %s\n", SETUP_AP_NAME, WiFi.softAPIP().toString().c_str());
}

static void closeSetup() {
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  setupOpen = false;
  Serial.println("Joined Wi-Fi; setup network closed.");
}

static std::string jsonText(const String& s) {
  std::string out;
  for (size_t i = 0; i < s.length(); i++) {
    char ch = s[i];
    if (ch == '"' || ch == '\\') out += '\\';
    if ((unsigned char)ch >= 0x20) out += ch;
  }
  return out;
}

// The scan runs in the background so the video and ONVIF keep being served; the page asks again until it is done.
static std::string networksJson() {
  std::string head = std::string("{\"name\":\"") + hubName.c_str() + ".local" + (webPort == 80 ? std::string() : ":" + std::to_string(webPort)) + "\",\"current\":\"" + jsonText(wifiSsid[0]) + "\",\"backup\":\"" + jsonText(wifiSsid[1]) + "\",";
  int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return head + "\"scanning\":true,\"networks\":[]}\n";
  if (n < 0) {
    WiFi.scanNetworks(true);
    return head + "\"scanning\":true,\"networks\":[]}\n";
  }
  std::string list;
  for (int i = 0; i < n && i < 20; i++) {
    if (i) list += ",";
    list += "{\"ssid\":\"" + jsonText(WiFi.SSID(i)) + "\",\"rssi\":" + std::to_string(WiFi.RSSI(i)) + ",\"open\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  WiFi.scanDelete();
  return head + "\"scanning\":false,\"networks\":[" + list + "]}\n";
}

static bool saveNvr(const std::string& ip) {
  Preferences p;
  if (!p.begin("net", false)) return false;
  p.putString("nvrip", ip.c_str());  // reading it back, because saving an empty value reports the same as a failure
  bool ok = p.getString("nvrip", "") == ip.c_str();
  p.end();
  return ok;
}

static void forgetStatic() {
  Preferences p;
  if (p.begin("net", false)) {
    p.remove("ipfor");
    p.end();
  }
}

static std::string addressJson() {
  return std::string("{\"type\":\"WiFi\",\"dhcp\":") + (usingStatic ? "false" : "true") + ",\"staticFor\":\"" + jsonText(staticFor) + "\",\"ip\":\"" + WiFi.localIP().toString().c_str() +
         "\",\"mask\":\"" + WiFi.subnetMask().toString().c_str() + "\",\"gateway\":\"" + WiFi.gatewayIP().toString().c_str() + "\",\"dns\":\"" + WiFi.dnsIP().toString().c_str() +
         "\",\"mac\":\"" + WiFi.macAddress().c_str() + "\"}\n";
}

static bool saveAddress(const CamAddress& a) {
  String network = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : wifiSsid[0];
  if (!a.dhcp && network.isEmpty()) return false;
  Preferences p;
  if (!p.begin("net", false)) return false;
  bool ok = true;
  if (a.dhcp) {
    p.remove("ipfor");
  } else {
    ok = p.putString("ipfor", network) == network.length() && p.putString("ip", a.ip.c_str()) == a.ip.size() && p.putString("mask", a.mask.c_str()) == a.mask.size() &&
         p.putString("gw", a.gateway.c_str()) == a.gateway.size() && p.putString("dns", a.dns.c_str()) == a.dns.size();
  }
  p.end();
  if (ok) {
    savedCause = 6;
    wifiSavedAt = millis();
    wifiSaved = true;
  }
  return ok;
}

static CamConfig makeConfig() {
  CamConfig c;
  c.onvifPort = onvifPort;
  c.webPort = webPort;
  c.settingsSave = saveSettings;
  c.espNowInfo = [] { return std::string("\"mac\":\"") + WiFi.macAddress().c_str() + "\",\"channel\":" + std::to_string(WiFi.channel()); };
  c.nvrIp = nvrIp;
  c.nvrSave = saveNvr;
  c.addressJson = addressJson;
  c.addressSave = saveAddress;
  if (webPort != 80) c.setupAddress += ":" + std::to_string(webPort);
  c.hostName = std::string(hubName.c_str()) + ".local";
  c.setupActive = [] { return setupOpen; };
  c.wifiNetworks = networksJson;
  c.wifiSave = saveWifi;
  c.rtspPort = rtspPort;
  c.holdMs = SENSOR_HOLD_MS;
  c.sensorToken = SENSOR_TOKEN;
  c.webPassword = webPassword.c_str();
  // Wi-Fi and lwIP fail badly when RAM runs out, so a large request must leave them room.
  // A request for 0 bytes asks only whether new work is safe, which needs less headroom.
  c.canAllocate = [](size_t n) { return n == 0 ? ESP.getFreeHeap() >= 10 * 1024 : ESP.getMaxAllocHeap() >= n + 1024 && ESP.getFreeHeap() >= n + 16 * 1024; };
  c.fsUsage = [](uint32_t& used, uint32_t& total) {
    used = LittleFS.usedBytes();
    total = LittleFS.totalBytes();
  };
  c.serial = macSerial();
  return c;
}

// A start time for timers where 0 means "not running". It must never be later than millis(), or the elapsed time wraps to a huge value.
static uint32_t timerStart() {
  uint32_t t = millis();
  return t == 0 ? 1 : t;
}

static const uint32_t WIFI_RESTART_MS = 5UL * 60UL * 1000UL;
static const uint32_t LOW_HEAP_BYTES = 8 * 1024;
static const uint32_t LOW_HEAP_RESTART_MS = 30000;
static volatile uint32_t wifiDrops = 0;
static volatile uint8_t wifiDropReason = 0;

// Survives a software restart, so the heartbeat can say which rule caused it: 1 Wi-Fi, 2 low RAM, 3 server stalled, 4 router silent, 5 new Wi-Fi settings, 6 new web, port or address settings, 7 fixed address could not reach the router.
RTC_NOINIT_ATTR static uint32_t restartCause;
static uint32_t lastRestartCause = 0;
static void restartFor(uint32_t cause) {
  restartCause = cause;
  ESP.restart();
}

static const uint32_t PING_LOST_RESTART_MS = 90000;
static volatile uint32_t pingReplies = 0, pingLost = 0, lastPingReply = 0;
static esp_ping_handle_t pingSession = nullptr;

// Wi-Fi can report "connected" while the network stack answers nothing, so the router is asked directly.
static void startGatewayPing() {
  esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
  config.target_addr.type = IPADDR_TYPE_V4;
  config.target_addr.u_addr.ip4.addr = (uint32_t)WiFi.gatewayIP();
  config.count = ESP_PING_COUNT_INFINITE;
  config.interval_ms = 20000;
  config.timeout_ms = 3000;
  esp_ping_callbacks_t callbacks = {};
  callbacks.on_ping_success = [](esp_ping_handle_t, void*) {
    lastPingReply = millis();
    pingReplies = pingReplies + 1;
  };
  callbacks.on_ping_timeout = [](esp_ping_handle_t, void*) { pingLost = pingLost + 1; };
  if (esp_ping_new_session(&config, &callbacks, &pingSession) == ESP_OK) esp_ping_start(pingSession);
}

static SensorCamServer* server;
static bool wasConnected = false;

struct SensorMessage {
  uint8_t mac[6];
  int8_t rssi;
  SensorReport report;
};
static QueueHandle_t sensorQueue;

// Runs on the Wi-Fi task, so it only queues. loop() owns all sensor state.
static void onEspNow(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  SensorMessage m;
  if (!parseSensorPacket(data, len, m.report)) return;
  memcpy(m.mac, info->src_addr, 6);
  m.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
  xQueueSend(sensorQueue, &m, 0);
}

void setup() {
  lastRestartCause = esp_reset_reason() == ESP_RST_SW ? restartCause : 0;
  restartCause = 0;
  Serial.begin(115200);
  loadWifi();
  loadSettings();
  WiFi.persistent(false);  // the settings are kept in one place only, by loadWifi and saveWifi
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // power save loses multicast and adds latency
  WiFi.setAutoReconnect(false);  // loop() chooses between the main and backup networks itself
  WiFi.setHostname(hubName.c_str());
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    wifiDrops = wifiDrops + 1;
    wifiDropReason = info.wifi_sta_disconnected.reason;
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  if (wifiSsid[0].length()) tryNetwork(0);
  configTime(0, 0, NTP_SERVER);

  // Formats on first use. Without storage the hub still runs, using its built-in pictures.
  if (!LittleFS.begin(true)) Serial.println("LittleFS could not start; using built-in pictures.");

  server = new SensorCamServer(makeConfig(), localIp, nowMs);
  if (!server->begin()) {
    Serial.println("Could not open the ONVIF, web or RTSP port; going back to the default ports and restarting.");
    forgetPorts();  // saved ports that cannot open must not keep the hub in a restart loop
    delay(2000);
    ESP.restart();
  }

  sensorQueue = xQueueCreate(32, sizeof(SensorMessage));
  if (esp_now_init() == ESP_OK) esp_now_register_recv_cb(onEspNow);
  else Serial.println("ESP-NOW failed to start; HTTP sensor input still works.");

  enableLoopWDT();  // an unattended hub must restart itself if the main loop ever stalls
}

void loop() {
  bool connected = WiFi.status() == WL_CONNECTED;
  bool justLost = !connected && wasConnected;
  if (connected && !wasConnected) {
    server->networkUp();
    connectedAt = timerStart();
    if (!pingSession) startGatewayPing();
    static bool nameAnnounced = false;
    if (!nameAnnounced && MDNS.begin(hubName.c_str())) {
      MDNS.addService("http", "tcp", webPort);
      nameAnnounced = true;
    }
  }
  wasConnected = connected;

  // An unattended hub must not stay off the network: keep asking, then start clean.
  static uint32_t lostAt = 0;
  static bool hadGuest = false;
  if (connected) {
    lostAt = 0;
    hadGuest = false;
    if (wifiTrying == 1) promoteBackup();
    if (setupOpen) closeSetup();
  } else {
    if (lostAt == 0) lostAt = timerStart();
    bool haveNetwork = wifiSsid[0].length() > 0;
    if (!haveNetwork) openSetup();
    if (justLost && haveNetwork) tryNetwork(0);
    // Connection attempts move the radio, which would drop whoever is on the setup network and block its scan.
    bool guest = setupOpen && WiFi.softAPgetStationNum() > 0;
    if (guest && !hadGuest) WiFi.disconnect(false);
    hadGuest = guest;
    if (guest) {
      lostAt = timerStart();  // someone is using the setup page; a restart would throw them off
    } else if (haveNetwork && millis() - wifiTryAt > WIFI_TRY_MS) {
      if (wifiTrying == 0 && wifiSsid[1].length() > 0) {
        tryNetwork(1);
      } else {  // neither network worked: open the setup network and keep trying behind it
        openSetup();
        tryNetwork(0);
      }
    }
    if (haveNetwork && millis() - lostAt > WIFI_RESTART_MS) restartFor(1);
  }
  if (setupOpen) dnsServer.processNextRequest();
  if (wifiSaved && millis() - wifiSavedAt > 1500) restartFor(savedCause);

  // Out of RAM the network stack stops working while this loop carries on, so the loop watchdog cannot see it.
  static uint32_t lowSince = 0;
  if (ESP.getFreeHeap() >= LOW_HEAP_BYTES) {
    lowSince = 0;
  } else {
    if (lowSince == 0) lowSince = timerStart();
    if (millis() - lowSince > LOW_HEAP_RESTART_MS) restartFor(2);
  }
  if (server->stalledMs(millis()) > 30000) restartFor(3);  // the loop runs but the server has stopped serving
  // Only after the router has answered at least once, so a router that ignores ping cannot cause restarts.
  // The reply time is read first because the ping task can update it at any moment, and a time later than millis() would wrap.
  // A wrong fixed address leaves Wi-Fi connected but the hub unreachable, with no other way back to it.
  if (connected && usingStatic && pingReplies == 0 && pingLost > 0 && millis() - connectedAt > STATIC_CHECK_MS) {
    forgetStatic();
    restartFor(7);
  }
  uint32_t lastReply = lastPingReply;
  if (connected && pingReplies > 0 && millis() - lastReply > PING_LOST_RESTART_MS) restartFor(4);

  static uint32_t printedAt = 0;
  if (millis() - printedAt > 10000) {
    printedAt = millis();
    Serial.printf("ONVIF on %s:%u, web port %u, RTSP port %u, ESP-NOW to %s on Wi-Fi channel %d, status %d, RSSI %d, drops %u, last drop reason %u, free heap %u, largest block %u, last reset reason %d cause %u, ping ok %u lost %u, setup %s, %s\n",
                  WiFi.localIP().toString().c_str(), onvifPort, webPort, rtspPort, WiFi.macAddress().c_str(), WiFi.channel(), (int)WiFi.status(), WiFi.RSSI(), (unsigned)wifiDrops,
                  (unsigned)wifiDropReason, ESP.getFreeHeap(), ESP.getMaxAllocHeap(), (int)esp_reset_reason(), (unsigned)lastRestartCause, (unsigned)pingReplies, (unsigned)pingLost, setupOpen ? "open" : "off", server->debugLine().c_str());
  }

  SensorMessage m;
  while (xQueueReceive(sensorQueue, &m, 0) == pdTRUE) server->espNowInput(m.mac, m.report, m.rssi);
  server->poll(20);
}
