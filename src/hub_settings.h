// The hub's saved settings: what is kept in flash (NVS), and the values loaded from it at start.
// These functions only store. Whether a change needs a restart, or the clock or Wi-Fi to be touched, is up to the caller.
#pragma once
#include <Arduino.h>
#include <string>
#include "cam_server.h"

struct HubSettings {
  String wifiSsid[2], wifiPassword[2];  // [0] is the main network, [1] the backup
  uint16_t onvifPort = 0, webPort = 0, rtspPort = 0;
  String hubName, webPassword, sensorToken;
  String espNowKey;  // sensor nodes sign their ESP-NOW messages with it
  bool espNowUnsigned = false;
  CamWebhook webhook;
  bool healthAlarm = false;
  // A fixed address belongs to one WiFi network, because the main and backup networks can use different address ranges.
  String staticFor, staticIp, staticMask, staticGateway, staticDns;
  std::string nvrIp;
  bool timeNtp = true;
  String ntpServer;
  int tzOffset = 0, tzDst = 0;
  CamSchedule schedule;
};
extern HubSettings settings;

// Settings saved on the web page win over the defaults compiled in from config.h and secrets.h. Makes the sensor
// token and the ESP-NOW key the first time.
void loadSettings();

bool storeSettings(const CamSettings& s);  // ports, name and web password
void forgetPorts();                        // keeps the name and password, which cannot stop the ports from opening
bool storeWifi();                          // both networks, as they are in settings
// A network saved on the page: the main network, whose old one becomes the backup, or the backup.
bool rememberWifi(const std::string& ssid, const std::string& password, bool backup);
bool storeTime(const CamTime& t);
bool storeSchedule(const CamSchedule& s);
bool storeNvr(const std::string& ip);
bool storeAddress(const CamAddress& a, const String& network);  // a fixed address for that network, or DHCP
void forgetStatic();
bool storeHealthAlarm(bool on);
bool storeEspNowUnsigned(bool on);
bool storeWebhook(const CamWebhook& w);
bool resetSettings();  // erases everything except what the sensors need to keep reporting

std::string backupText();
bool restoreText(const std::string& text);  // all or nothing
