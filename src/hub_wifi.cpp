#include <Arduino.h>  // first, before the network headers the hub's library brings

#include "hub_wifi.h"
#include <DNSServer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <atomic>
#include "cam_board_rules.h"
#include "cam_timing.h"
#include "config.h"
#include "hub_restart.h"
#include "hub_settings.h"

static CamWifiPlan plan;       // which network to try, and when the setup network opens: cam_board_rules.h
static CamSetupButton button;  // a blank hub's open setup network waits for it: camSetupMayOpen
static DNSServer dnsServer;
static bool setupOpen = false;
static bool usingStatic = false;
// Counted on the Wi-Fi task, read in the loop.
static std::atomic<uint32_t> drops{0};
static std::atomic<uint8_t> dropReason{0};
static uint32_t dropsAtTry = 0;  // so a drop since the last try can be told from one before it

static void tryNetwork(int i) {
  WiFi.disconnect(false);
  dropsAtTry = drops;
  IPAddress ip, mask, gateway, dns;
  usingStatic = settings.staticFor.length() && settings.staticFor == settings.wifiSsid[i] && ip.fromString(settings.staticIp) &&
                mask.fromString(settings.staticMask) && gateway.fromString(settings.staticGateway);
  // DHCP is asked for with the empty address, 0.0.0.0. Not INADDR_NONE: after the network headers that is lwIP's,
  // 255.255.255.255, which Wi-Fi took as a fixed address, so the hub joined a network without one and was never given
  // an address.
  if (usingStatic)
    WiFi.config(ip, gateway, mask, dns.fromString(settings.staticDns) ? dns : gateway);
  else
    WiFi.config(IPAddress(), IPAddress(), IPAddress());
  WiFi.begin(settings.wifiSsid[i].c_str(), settings.wifiPassword[i].c_str());
}

void wifiBegin() {
#if SETUP_BUTTON_PIN >= 0
  pinMode(SETUP_BUTTON_PIN, INPUT_PULLUP);
#else
  button.fitted = false;
#endif
  WiFi.persistent(false);  // the settings are kept in one place only, in hub_settings.cpp
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);          // power save loses multicast and adds latency
  WiFi.setAutoReconnect(false);  // keepOnNetwork chooses between the main and backup networks itself
  WiFi.setHostname(settings.hubName.c_str());
  WiFi.onEvent(
      [](WiFiEvent_t, WiFiEventInfo_t info) {
        drops++;
        dropReason = info.wifi_sta_disconnected.reason;
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  if (settings.wifiSsid[0].length()) {
    tryNetwork(0);
    plan.triedFromStart(millis());
  }
}

// The backup worked, so it becomes the main network and the old main becomes the backup.
static void promoteBackup() {
  std::swap(settings.wifiSsid[0], settings.wifiSsid[1]);
  std::swap(settings.wifiPassword[0], settings.wifiPassword[1]);
  bool ok = storeWifi();
  Serial.printf("Joined the backup network; it is now the main network%s.\n", ok ? "" : " until the next restart (could not save)");
}

// WPA3 only, with protected management frames as WPA3 requires. Under WPA2, anyone who records a device joining could try
// passwords offline, as fast as their computer allows, and this password is usually the hub password. WPA3 lets them
// try only by asking the hub, one guess at a time. Should WPA3 not start, WPA2 is better than no setup network, as it
// is the way back onto the network.
static void startSetupNetwork(const std::string& password) {
  if (password.empty()) {
    WiFi.softAP(SETUP_AP_NAME);
    return;
  }
  wifi_config_t conf;
  bool wpa3 = WiFi.softAP(SETUP_AP_NAME, password.c_str(), 1, 0, 4, false, WIFI_AUTH_WPA3_PSK) && esp_wifi_get_config(WIFI_IF_AP, &conf) == ESP_OK;
  if (wpa3) {
    conf.ap.pmf_cfg.capable = true;
    conf.ap.pmf_cfg.required = true;
    conf.ap.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    wpa3 = esp_wifi_set_config(WIFI_IF_AP, &conf) == ESP_OK;
  }
  if (!wpa3) {
    Serial.println("The setup network could not start with WPA3; it uses WPA2 instead.");
    WiFi.softAP(SETUP_AP_NAME, password.c_str());
  }
}

// Pressed: a blank hub's setup network may open, for CAM_OPEN_SETUP_MS from the last press.
static void watchButton(uint32_t now) {
#if SETUP_BUTTON_PIN >= 0
  if (digitalRead(SETUP_BUTTON_PIN) == LOW) {
    if (!button.pressed) Serial.println("The button was pressed; a blank hub's setup network may open for 15 minutes.");
    button.pressed = true;
    button.pressedAt = now;
  }
#else
  (void)now;
#endif
}

// The setup network's password, from camSetupKey: open on a blank hub, whose setup page asks for a password first, and
// then only after its button (camSetupMayOpen).
static void openSetup() {
  std::string password;
  CamSetupKey key = camSetupKey(SETUP_AP_PASSWORD, settings.webPassword.c_str(), password);
  if (setupOpen || !camSetupMayOpen(key, button, millis(), 0)) {
    static uint32_t toldAt = 0;
    if (!setupOpen && key == CAM_SETUP_OPEN && (!toldAt || millis() - toldAt > 60000)) {
      toldAt = millis();
      Serial.println("Press the hub's BOOT button to open its setup network.");
    }
    return;
  }
  WiFi.disconnect(false);
  WiFi.mode(WIFI_AP_STA);
  startSetupNetwork(password);
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

bool keepOnNetwork(bool connected, bool justLost) {
  CamWifiPlan::Pass p;
  p.now = millis();
  p.connected = connected;
  p.justLost = justLost;
  p.turnedAway = drops != dropsAtTry && dropReason == WIFI_REASON_ASSOC_COMEBACK_TIME_TOO_LONG;
  p.haveMain = settings.wifiSsid[0].length() > 0;
  p.haveBackup = settings.wifiSsid[1].length() > 0;
  p.setupOpen = setupOpen;
  p.setupStations = setupOpen ? WiFi.softAPgetStationNum() : 0;
  watchButton(p.now);
  // A blank hub's open setup network closes once its time is up and nobody is on it.
  std::string password;
  if (setupOpen && !camSetupMayOpen(camSetupKey(SETUP_AP_PASSWORD, settings.webPassword.c_str(), password), button, p.now, p.setupStations)) {
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    setupOpen = false;
    p.setupOpen = false;
    Serial.println("The setup network's 15 minutes are up; press the button to open it again.");
  }
  int act = plan.step(p);
  if (act & CamWifiPlan::PROMOTE_BACKUP) promoteBackup();
  if (act & CamWifiPlan::CLOSE_SETUP) closeSetup();
  if (act & CamWifiPlan::OPEN_SETUP) openSetup();
  if (act & CamWifiPlan::TRY_MAIN) tryNetwork(0);
  if (act & CamWifiPlan::TRY_BACKUP) tryNetwork(1);
  if (act & CamWifiPlan::STOP_TRYING) WiFi.disconnect(false);  // tries move the radio, which would drop whoever is on the setup network
  return plan.guest();
}

void serveSetupNetwork() {
  if (setupOpen) dnsServer.processNextRequest();
}

// Joined, and with an address to be reached at. Joined without one, the hub can reach neither the NVR nor anything
// else, so it counts as off the network: the plan then tries the backup and opens the setup network, as for a network
// that cannot be joined.
bool onNetwork() {
  static bool told = false;
  if (WiFi.status() != WL_CONNECTED) {
    told = false;
    return false;
  }
  uint32_t ip = WiFi.localIP();
  bool addressed = ip != 0 && ip != 0xFFFFFFFF;
  if (!addressed && !told) Serial.printf("Joined %s, but without an address; trying on as if off the network.\n", WiFi.SSID().c_str());
  told = !addressed;
  return addressed;
}

bool setupIsOpen() { return setupOpen; }
bool usingStaticAddress() { return usingStatic; }
uint32_t wifiDrops() { return drops; }
uint8_t wifiDropReason() { return dropReason; }

// The scan runs in the background so the video and ONVIF keep being served; the page asks again until it is done.
void networksInfo(CamJson& j) {
  j.str("name", std::string(settings.hubName.c_str()) + ".local" + (settings.webPort == 80 ? std::string() : ":" + std::to_string(settings.webPort)));
  j.str("current", settings.wifiSsid[0].c_str()).str("backup", settings.wifiSsid[1].c_str());
  int16_t n = WiFi.scanComplete();
  if (n < 0 && n != WIFI_SCAN_RUNNING) WiFi.scanNetworks(true);
  j.flag("scanning", n < 0).array("networks");
  for (int i = 0; i < n && i < 20; i++)
    j.object().str("ssid", WiFi.SSID(i).c_str()).num("rssi", WiFi.RSSI(i)).flag("open", WiFi.encryptionType(i) == WIFI_AUTH_OPEN).end();
  if (n >= 0) WiFi.scanDelete();
  j.end();
}

bool saveWifi(const std::string& ssid, const std::string& password, bool backup) {
  if (!rememberWifi(ssid, password, backup)) return false;
  if (!backup || WiFi.status() != WL_CONNECTED) requestRestart(RESTART_NEW_WIFI);  // a new backup needs no restart while the main network is working
  return true;
}

void addressInfo(CamJson& j) {
  j.str("type", "WiFi").flag("dhcp", !usingStatic).str("staticFor", settings.staticFor.c_str()).str("ip", WiFi.localIP().toString().c_str());
  j.str("mask", WiFi.subnetMask().toString().c_str()).str("gateway", WiFi.gatewayIP().toString().c_str()).str("dns", WiFi.dnsIP().toString().c_str());
  j.str("mac", WiFi.macAddress().c_str());
}

bool saveAddress(const CamAddress& a) {
  String network = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : settings.wifiSsid[0];
  if (!a.dhcp && network.isEmpty()) return false;
  bool ok = storeAddress(a, network);
  if (ok) requestRestart(RESTART_NEW_SETTINGS);
  return ok;
}
