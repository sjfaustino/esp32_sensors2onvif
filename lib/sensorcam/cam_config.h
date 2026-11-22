// What the board gives the hub: its settings at the start, and the chip, radio and storage parts it works through.
#pragma once
#include <stdint.h>
#include <time.h>
#include <string>
#include "cam_json.h"
#include "cam_soap.h"

// Every setting the hub keeps in flash and a backup holds, in one table: its place, its kind, and what a good value is.
// kind is s for text, u a 16-bit number, i a number, b true or false. A number is good within min and max; a text when
// check says so. The pages' checks of what they save, the backup's checks of what it restores, and the list of what a
// backup holds all come from here; tools/check_settings.py checks that hub_settings.cpp loads and saves each one.
struct CamSetting {
  const char* space;
  const char* key;
  char kind;
  long long min, max;
  bool (*check)(const std::string& value);
};
extern const CamSetting CAM_SETTINGS[];
extern const size_t CAM_SETTING_COUNT;
const CamSetting* camSetting(const std::string& key);  // key is "space.key", such as "net.web"; nullptr for none
// Whether a value, as text, is good for that setting, as the page and a backup give it. False for an unknown key.
bool camSettingValid(const std::string& key, const std::string& value);
// The same for a number, read into out when it is good.
bool camSettingNumber(const std::string& key, const std::string& value, long long& out);
bool camBackupValueValid(const std::string& key, const std::string& value);  // camSettingValid, for the backup
bool camBackupKeyKnown(const std::string& key);
// The table, to go through with a for loop.
struct CamSettingList {
  const CamSetting* begin() const { return CAM_SETTINGS; }
  const CamSetting* end() const { return CAM_SETTINGS + CAM_SETTING_COUNT; }
};
inline CamSettingList camSettings() { return {}; }

const uint32_t CAM_MAX_DISARM_MINUTES = 7 * 1440;
// A disarm with an end, read back after a restart: until is when it ends in seconds since 1970, or 1 when the clock was
// not set when it began. Over once that time has passed; and when how long is left cannot be known, because it began
// without a clock or the clock has not been set within startWaitMs of the start, it is over at once, as a false alert
// is better than a missed one.
bool camTimedDisarmOver(int64_t until, time_t wallNow, uint32_t msSinceStart, uint32_t startWaitMs);
std::string camMinutesText(uint32_t minutes);  // "30 min", "2 h", "1 day"

// Copies text into a fixed buffer, shortened if needed without splitting a UTF-8 character.
void camCopyText(char* out, size_t size, const std::string& text);
void camCopyText(char* out, size_t size, const char* text);  // without using the heap

struct CamSettings {
  uint16_t onvifPort = 0;
  uint16_t webPort = 0;
  uint16_t rtspPort = 0;
  std::string name;  // empty keeps the current name
  bool passwordOn = false;
  std::string password;     // empty keeps the current password while protection stays on
  bool hostsGiven = false;  // otherwise the other names are kept
  std::string hosts;        // other names the web port answers to, comma-separated
  // A port or the name changed, which every service listens on or announces, so the hub restarts. A new password or
  // other names apply at once.
  bool restart = false;
};

struct CamTime {
  bool ntp = true;
  std::string server;     // empty keeps the current NTP server
  int offsetMinutes = 0;  // east of UTC. Only display and the scheduled restart use it; the NVR always gets UTC.
  int dst = 0;            // 0 none, 1 Europe, 2 United States
  long long epoch = 0;    // manual mode: the time to set, or 0 to leave the clock alone
};

struct CamSchedule {
  bool on = false;
  int day = -1;    // -1 every day, otherwise 0 is Sunday
  int minute = 0;  // minutes after midnight, local time
};

// How each message is sent: JSON for Home Assistant and Node-RED, plain text, or as an ntfy or Gotify server takes it,
// with a title and a priority for the phone.
enum CamHookFormat : int { CAM_HOOK_JSON = 0, CAM_HOOK_TEXT = 1, CAM_HOOK_NTFY = 2, CAM_HOOK_GOTIFY = 3 };

// Messages to a web address of the user's choosing, such as Home Assistant, Node-RED, ntfy or Gotify.
struct CamWebhook {
  std::string url;             // http:// with an IPv4 address; empty sends nothing
  int format = CAM_HOOK_JSON;  // a CamHookFormat
  std::string token;           // ntfy's access token, or Gotify's application token; empty for none
  bool alerts = true;          // sensor alerts
  bool problems = true;        // silent nodes, low batteries and weak signals
  int dailyMinute = -1;        // the daily check, in minutes after local midnight; -1 sends none
  int weeklyDay = -1;          // the weekly summary's day, 0 for Sunday to 6; -1 sends none
  int weeklyMinute = 9 * 60;   // and its time, in minutes after local midnight
};

// An MQTT broker, through which Home Assistant finds the hub and its sensors by itself.
struct CamMqtt {
  std::string host;  // the broker's IPv4 address; empty turns MQTT off
  uint16_t port = 1883;
  std::string user;  // empty logs in without a user name or password
  std::string password;
};

// The hub's health, for Home Assistant's diagnostic sensors. Bytes, except where it says otherwise.
struct CamHealth {
  uint32_t freeHeap = 0;
  uint32_t lowestFreeHeap = 0;     // since the start
  uint32_t largestBlock = 0;       // what one allocation can have now
  uint32_t worstLargestBlock = 0;  // the least that has been, since the start: how cut up the memory has got
  uint32_t espNowDropped = 0;      // arrived while the hub was busy; their senders resend
  int wifiRssi = 0;                // dBm; 0 when not connected
  uint64_t uptimeS = 0;
};

enum CamArmMode : uint8_t { CAM_DISARMED, CAM_ARMED_AWAY, CAM_ARMED_HOME };
const char* camArmModeName(CamArmMode mode);  // "disarmed", "away" or "home", as the API gives it

// How often memory ran out and only that piece of work was dropped, never the hub. One count, which the server owns and
// every part adds to, for the pages and Home Assistant.
class CamMemoryFailures {
 public:
  void note() { count_++; }
  uint32_t count() const { return count_; }

 private:
  uint32_t count_ = 0;
};

struct CamAddress {
  bool dhcp = true;
  std::string ip, mask, gateway, dns;  // already validated; dns may be empty, meaning the gateway
};

// What the board does for the hub, in three parts: the chip, its radio and network, and where settings are kept. Each
// method's default is what a board without it does, so the feature it serves is refused or left out: a PC build or a
// test overrides only what it uses, and a part left out of CamConfig is one with every default.

// The chip: memory, storage, firmware updates, the crash report, restarts, the clock and Device Info.
class CamChip {
 public:
  virtual ~CamChip() = default;
  virtual bool storageUsage(uint32_t& used, uint32_t& total) { return false; }  // false: not known, and nothing is limited by it
  virtual bool canAllocate(size_t bytes) { return true; }                       // lets large requests be refused before they exhaust RAM
  virtual bool hasHeadroom() { return true; }                                   // whether new connections and video frames are safe right now
  virtual bool firmwareBegin(size_t size) { return false; }
  virtual bool firmwareWrite(const uint8_t* data, size_t len) { return false; }
  virtual bool firmwareEnd(bool commit) { return false; }  // commit restarts the hub shortly after the reply
  virtual size_t crashSize() { return 0; }                 // bytes of the crash report the chip saved to flash; 0 when there is none
  virtual bool crashRead(size_t offset, char* out, size_t len) { return false; }
  virtual bool crashDelete() { return false; }
  virtual bool maintenance(const std::string& action) { return false; }  // "reboot", "reset" or "newkeys"; restarts shortly after replying
  virtual void deviceRows(CamJson& j) {}                                 // ["label","value"] arrays for the Device Info page
  virtual bool random32(uint32_t& out) { return false; }                 // a number from the chip's random source; false where there is none
  virtual bool tellsHealth() { return false; }                           // health() has figures, for Home Assistant's diagnostic sensors
  virtual void health(CamHealth& h) {}
  virtual time_t wallTime() { return time(nullptr); }  // seconds since 1970 UTC; before the clock is set, a small number
  virtual int localMinute() { return -1; }             // minutes after local midnight, or -1 when the clock is not set; for quiet hours
  virtual int localWeekday() { return -1; }            // 0 for Sunday to 6, or -1 when the clock is not set
  virtual void timeInfo(CamJson& j) {}                 // the Time page's members
  virtual bool timeSave(const CamTime& t) { return false; }
};

// The radio: Wi-Fi and its setup network, the hub's address, and ESP-NOW.
class CamRadio {
 public:
  virtual ~CamRadio() = default;
  virtual bool setupActive() { return false; }  // the setup network is open
  virtual void wifiNetworks(CamJson& j) {}      // the setup page's members: name, current, backup, scanning, networks
  virtual bool wifiSave(const std::string& ssid, const std::string& password, bool backup) { return false; }
  virtual void addressInfo(CamJson& j) {}                                      // the Network page's members
  virtual bool addressSave(const CamAddress& a) { return false; }              // stores DHCP or a fixed address; applies after a restart
  virtual void onvifNetwork(SoapContext& s) {}                                 // fills in the hub's network details for ONVIF clients
  virtual void espNowInfo(CamJson& j) { j.str("mac", "").num("channel", 0); }  // the Nodes and pairing page's: the hub's mac and channel
  virtual int wifiChannel() { return 0; }                                      // told to nodes in the pairing answer
  virtual bool hasEspNow() { return false; }
  // Sends an ESP-NOW message, a confirmation, to a sensor node. Called from the task that calls poll().
  virtual void espNowSend(const uint8_t mac[6], const uint8_t* data, int len) {}
  // The Long Range radio test, /api/radio-test: start it, or say how it is going or went, as members of j. False when it
  // cannot start: running already, or the hub is off its Wi-Fi network.
  virtual bool hasRadioTest() { return false; }
  virtual bool radioTest(bool start, CamJson& j) { return false; }
};

// Where settings are kept, so they last through a restart. Each is false when they could not be kept.
class CamStore {
 public:
  virtual ~CamStore() = default;
  virtual bool settingsSave(const CamSettings& s) { return false; }         // new settings; they apply after a restart
  virtual bool passwordSave(const std::string& password) { return false; }  // a blank hub's first password; it applies after a restart
  virtual bool healthSave(bool alarm) { return false; }
  virtual std::string backupText() { return std::string(); }
  virtual bool restoreText(const std::string& text) { return false; }
  virtual bool scheduleSave(const CamSchedule& s) { return false; }
  virtual bool webhookSave(const CamWebhook& w) { return false; }
  virtual bool armedSave(CamArmMode mode, int64_t until) { return false; }
  virtual bool rearmSave(uint32_t minutes) { return false; }
  virtual bool mqttSave(const CamMqtt& m) { return false; }
  virtual bool nvrSave(const std::string& ip) { return false; }
  // The first number the next start may give a sealed message, kept in flash and never in a backup. Each number seals
  // one message for good: used twice with a node's key, the two could be compared. So the hub reserves numbers a block
  // at a time, ahead of using them, and a start, even after a power cut, begins after the last block reserved.
  virtual bool sealReserve(uint32_t next) { return false; }
};

// The hub's settings at the start, which do not change while it runs. A setting the pages change later is held by the
// part it concerns from then on: the health alarm and the re-arm time by CamAlarm, the NVR address by CamOnvifEvents, the
// webhook by CamWebhookSender, MQTT by CamMqttClient, the restart schedule by CamWeb.
struct CamConfig {
  CamChip* chip = nullptr;  // the board's parts, which outlive the server; any may be left out
  CamRadio* radio = nullptr;
  CamStore* store = nullptr;
  std::string fsRoot = "/littlefs";
  std::string webPassword;                   // when set, the file manager requires Basic auth as "admin"
  std::string hostName;                      // the name shown in the web menu, such as esp-alarmhub.local
  std::string setupAddress = "192.168.4.1";  // where visitors to the setup network are sent
  uint16_t onvifPort = 8899;                 // ONVIF requests only
  uint16_t webPort = 80;                     // the file manager, sensor input, status and events
  uint16_t captivePort = 80;                 // answered only while the setup network is open, because phones probe it; 0 for none
  std::string onvifPassword;                 // when set, ONVIF and RTSP need the user admin and this password
  bool healthAlarm = false;                  // a silent node or low battery also raises an NVR alarm
  CamSchedule schedule;
  std::string espNowKey;   // signs the ESP-NOW messages of nodes not paired
  std::string pairingKey;  // the hub's X25519 private key, as 64 hex digits; empty turns pairing off
  CamWebhook webhook;
  // SHA-1 of the setup network's password, which is built into the firmware, so the Overview can tell a web password
  // that is still that one. Empty when there is none.
  std::string setupPasswordHash;
  CamMqtt mqtt;
  // Disarmed, alerts are logged but not sent to the NVR or the webhook, as in quiet hours. Kept across restarts.
  // Armed home, interior sensors are logged but not sent to the NVR.
  CamArmMode armMode = CAM_ARMED_AWAY;
  int64_t disarmedUntil = 0;     // see camTimedDisarmOver; 0 for a disarm without an end
  uint32_t rearmMinutes = 0;     // how long a disarm from Home Assistant lasts; 0 until armed again
  std::string firmwareVersion;   // told to Home Assistant
  std::string firmwareKey;       // the Ed25519 public key, as 64 hex digits, that signs updates (s2o_fw_sig.h)
  uint32_t firmwareRelease = 0;  // this firmware's release number; an update with a lower one is refused
  std::string nvrIp;             // only for the connection status on the NVR page; the hub never contacts the NVR
  uint16_t rtspPort = 8554;
  uint16_t discoveryPort = 3702;
  bool loopbackOnly = false;  // PC test build: nothing leaves this machine
  uint32_t holdMs = 10000;
  uint32_t silentMs = 3UL * 3600UL * 1000UL;  // a known sensor quiet for this long is reported as silent
  std::string sensorToken;                    // when set, /sensor requires token=<value>
  std::string serial = "000000000000";
  std::string startReason;     // why the hub last started, for the "hub started" event
  bool startPlanned = false;   // asked for: an update, the restart button, new settings; not a crash or a power cut
  std::string snapshotSecret;  // random and kept on the hub; keys the snapshot address's token
  uint32_t sealStart = 0;      // the first number of the hub's sealed messages: where the last start's left off, see sealReserve
  bool sealStartKept = true;   // false when the start could not save the block it reserved: nothing is sealed until one is
  std::string extraHosts;      // other names the web port answers to, comma-separated, such as a router's name for it
  bool watchNvr = true;        // log and send alerts the NVR was not listening for
};
