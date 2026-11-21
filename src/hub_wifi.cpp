#include <Arduino.h>  // first, before the network headers the hub's library brings

#include "hub_wifi.h"
#include <DNSServer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "cam_timing.h"
#include "config.h"
#include "hub_restart.h"
#include "hub_settings.h"

static int trying = 0;  // the network being tried: 0 the main one, 1 the backup
static uint32_t tryAt = 0;
static DNSServer dnsServer;
static bool setupOpen = false;
static bool usingStatic = false;
static bool hadGuest = false;
static volatile uint32_t drops = 0;
static volatile uint8_t dropReason = 0;

static void tryNetwork(int i) {
  trying = i;
  tryAt = millis();
  WiFi.disconnect(false);
  IPAddress ip, mask, gateway, dns;
  usingStatic = settings.staticFor.length() && settings.staticFor == settings.wifiSsid[i] && ip.fromString(settings.staticIp) &&
                mask.fromString(settings.staticMask) && gateway.fromString(settings.staticGateway);
  if (usingStatic)
    WiFi.config(ip, gateway, mask, dns.fromString(settings.staticDns) ? dns : gateway);
  else
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.begin(settings.wifiSsid[i].c_str(), settings.wifiPassword[i].c_str());
}

void wifiBegin() {
  WiFi.persistent(false);  // the settings are kept in one place only, in hub_settings.cpp
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);          // power save loses multicast and adds latency
  WiFi.setAutoReconnect(false);  // keepOnNetwork chooses between the main and backup networks itself
  WiFi.setHostname(settings.hubName.c_str());
  WiFi.onEvent(
      [](WiFiEvent_t, WiFiEventInfo_t info) {
        drops = drops + 1;
        dropReason = info.wifi_sta_disconnected.reason;
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  if (settings.wifiSsid[0].length()) tryNetwork(0);
}

// The backup worked, so it becomes the main network and the old main becomes the backup.
static void promoteBackup() {
  std::swap(settings.wifiSsid[0], settings.wifiSsid[1]);
  std::swap(settings.wifiPassword[0], settings.wifiPassword[1]);
  trying = 0;
  bool ok = storeWifi();
  Serial.printf("Joined the backup network; it is now the main network%s.\n", ok ? "" : " until the next restart (could not save)");
}

// The setup network's password: one from secrets.h, else the hub password. A blank hub has neither, so its setup network
// is open and its setup page asks for a hub password first. A password too short for WPA2, from an old firmware, keeps
// the setup network off rather than open.
static bool setupPassword(String& out) {
  out = strlen(SETUP_AP_PASSWORD) >= 8 ? String(SETUP_AP_PASSWORD) : settings.webPassword;
  return out.isEmpty() || out.length() >= 8;
}

// WPA3 only, with protected management frames as WPA3 requires. Under WPA2, anyone who records a device joining could try
// passwords offline, as fast as their computer allows, and this password is usually the hub password. WPA3 lets them
// try only by asking the hub, one guess at a time. Should WPA3 not start, WPA2 is better than no setup network, as it
// is the way back onto the network.
static void startSetupNetwork(const String& password) {
  if (password.isEmpty()) {
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

static void openSetup() {
  String password;
  if (setupOpen || !setupPassword(password)) return;
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
  if (connected) {
    hadGuest = false;
    if (trying == 1) promoteBackup();
    if (setupOpen) closeSetup();
    return false;
  }
  bool haveNetwork = settings.wifiSsid[0].length() > 0;
  if (!haveNetwork) openSetup();
  if (justLost && haveNetwork) tryNetwork(0);
  // Connection attempts move the radio, which would drop whoever is on the setup network and block its scan.
  bool guest = setupOpen && WiFi.softAPgetStationNum() > 0;
  if (guest && !hadGuest) WiFi.disconnect(false);
  hadGuest = guest;
  if (!guest && haveNetwork && millis() - tryAt > CAM_WIFI_TRY_MS) {
    if (trying == 0 && settings.wifiSsid[1].length() > 0) {
      tryNetwork(1);
    } else {  // neither network worked: open the setup network and keep trying behind it
      openSetup();
      tryNetwork(0);
    }
  }
  return guest;
}

void serveSetupNetwork() {
  if (setupOpen) dnsServer.processNextRequest();
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
