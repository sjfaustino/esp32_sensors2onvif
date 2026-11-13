// ONVIF device, RTSP stream, sensor input and WS-Discovery in one non-blocking loop.
#pragma once
#include <stdint.h>
#include <time.h>
#include <functional>
#include <string>
#include <vector>
#include "cam_auth.h"
#include "cam_delivery.h"
#include "cam_events.h"
#include "cam_json.h"
#include "cam_rtp.h"
#include "cam_sensors.h"
#include "cam_soap.h"
#include "cam_storage.h"
#include "cam_subs.h"
#include "cam_timing.h"


// Whether a value from a settings backup is one the web pages would also accept. key is "space.key", such as "net.web".
bool camBackupValueValid(const std::string& key, const std::string& value);

// Copies text into a fixed buffer, shortened if needed without splitting a UTF-8 character.
void camCopyText(char* out, size_t size, const std::string& text);

struct CamSettings {
  uint16_t onvifPort = 0;
  uint16_t webPort = 0;
  uint16_t rtspPort = 0;
  std::string name;      // empty keeps the current name
  bool passwordOn = false;
  std::string password;  // empty keeps the current password while protection stays on
  bool hostsGiven = false;  // otherwise the other names are kept
  std::string hosts;         // other names the web port answers to, comma-separated
};

struct CamTime {
  bool ntp = true;
  std::string server;      // empty keeps the current NTP server
  int offsetMinutes = 0;   // east of UTC. Only display and the scheduled restart use it; the NVR always gets UTC.
  int dst = 0;             // 0 none, 1 Europe, 2 United States
  long long epoch = 0;     // manual mode: the time to set, or 0 to leave the clock alone
};

struct CamSchedule {
  bool on = false;
  int day = -1;    // -1 every day, otherwise 0 is Sunday
  int minute = 0;  // minutes after midnight, local time
};

// Messages to a web address of the user's choosing, such as Home Assistant, Node-RED or ntfy.
struct CamWebhook {
  std::string url;        // http:// with an IPv4 address; empty sends nothing
  bool text = false;      // plain text instead of JSON
  bool alerts = true;     // sensor alerts
  bool problems = true;   // silent nodes, low batteries and weak signals
};

struct CamAddress {
  bool dhcp = true;
  std::string ip, mask, gateway, dns;  // already validated; dns may be empty, meaning the gateway
};

// What the board provides. Any std::function may be left unset: the feature it serves is then refused or left out,
// so a PC build or a test sets only what it uses.
struct CamConfig {
  std::string fsRoot = "/littlefs";
  std::string webPassword;  // when set, the file manager requires Basic auth as "admin"
  std::function<void(uint32_t& used, uint32_t& total)> fsUsage;
  std::function<bool(size_t)> canAllocate;  // lets large requests be refused before they exhaust RAM
  std::function<bool()> hasHeadroom;        // whether new connections and video frames are safe right now
  // Wi-Fi setup page. The board supplies these; the setup network is the hub's own access point.
  std::function<bool()> setupActive;           // the setup network is open
  std::string hostName;                        // the name shown in the web menu, such as esp-alarmhub.local
  std::string setupAddress = "192.168.4.1";    // where visitors to the setup network are sent
  std::function<void(CamJson&)> wifiNetworks;  // the setup page's members: name, current, backup, scanning, networks
  std::function<bool(const std::string& ssid, const std::string& password, bool backup)> wifiSave;
  uint16_t onvifPort = 8899;  // ONVIF requests only
  uint16_t webPort = 80;      // the file manager, sensor input, status and events
  uint16_t captivePort = 80;  // answered only while the setup network is open, because phones probe it; 0 for none
  std::function<bool(const CamSettings&)> settingsSave;  // stores new settings; they apply after a restart
  std::string onvifPassword;  // when set, ONVIF and RTSP need the user admin and this password
  bool healthAlarm = false;   // a silent node or low battery also raises an NVR alarm
  std::function<bool(bool)> healthSave;
  std::function<bool(size_t size)> firmwareBegin;
  std::function<bool(const uint8_t* data, size_t len)> firmwareWrite;
  std::function<bool(bool commit)> firmwareEnd;  // commit restarts the hub shortly after the reply
  std::function<std::string()> backupText;
  std::function<bool(const std::string& text)> restoreText;
  std::function<void(SoapContext&)> onvifNetwork;  // fills in the hub's network details for ONVIF clients
  std::function<void(CamJson&)> timeInfo;      // the Time page's members
  std::function<bool(const CamTime&)> timeSave;
  CamSchedule schedule;
  std::function<bool(const CamSchedule&)> scheduleSave;
  std::function<void(CamJson&)> deviceRows;    // ["label","value"] arrays for the Device Info page
  std::function<size_t()> crashSize;           // bytes of the crash report the chip saved to flash; 0 when there is none
  std::function<bool(size_t offset, char* out, size_t len)> crashRead;
  std::function<bool()> crashDelete;
  std::function<bool(const std::string& action)> maintenance;  // "reboot" or "reset"; restarts shortly after replying
  std::function<void(CamJson&)> espNowInfo;  // members for the ESP-NOW page: the hub's mac and channel
  std::string espNowKey;                    // signs version 2 ESP-NOW messages
  // Sends an ESP-NOW message, a confirmation, to a sensor node. Call it from the task that calls poll().
  std::function<void(const uint8_t mac[6], const uint8_t* data, int len)> espNowSend;
  bool espNowUnsigned = false;              // also accept unsigned version 1 messages
  std::function<bool(bool)> espNowSave;     // stores espNowUnsigned
  CamWebhook webhook;
  std::function<bool(const CamWebhook&)> webhookSave;
  std::function<int()> localMinute;         // minutes after local midnight, or -1 when the clock is not set; for quiet hours
  std::string nvrIp;  // only for the connection status on the NVR page; the hub never contacts the NVR
  std::function<bool(const std::string& ip)> nvrSave;
  std::function<void(CamJson&)> addressInfo;            // the Network page's members
  std::function<bool(const CamAddress&)> addressSave;    // stores DHCP or a fixed address; applies after a restart
  uint16_t rtspPort = 8554;
  uint16_t discoveryPort = 3702;
  bool loopbackOnly = false;  // PC test build: nothing leaves this machine
  uint32_t holdMs = 10000;
  uint32_t silentMs = 3UL * 3600UL * 1000UL;  // a known sensor quiet for this long is reported as silent
  std::string sensorToken;  // when set, /sensor requires token=<value>
  std::string serial = "000000000000";
  std::string startReason;  // why the hub last started, for the "hub started" event
  std::string snapshotSecret;  // random and kept on the hub; keys the snapshot address's token
  std::string extraHosts;      // other names the web port answers to, comma-separated, such as a router's name for it
  bool watchNvr = true;     // log and send alerts the NVR was not listening for
};

class SensorCamServer {
 public:
  SensorCamServer(const CamConfig& cfg, std::function<std::string()> localIp, std::function<uint32_t()> millisFn);
  bool begin();
  void poll(int maxWaitMs);  // call continuously
  void networkUp();          // call after each Wi-Fi (re)connect
  // Milliseconds since the socket wait last worked. A large value means the server has stopped serving.
  uint32_t stalledMs(uint32_t now) const { return now - lastGoodWait_; }
  std::string debugLine();   // counters for the serial heartbeat
  void setOnvifPassword(const std::string& password) { cfg_.onvifPassword = password; }  // for the PC tests
  void espNowPacket(const uint8_t mac[6], const uint8_t* data, int len, int8_t rssi = 0);  // a raw ESP-NOW message; call from the same task as poll()
  void saveNow();  // writes any events not yet saved; call before a restart
  void networkLost(uint32_t seconds);  // call on rejoining Wi-Fi after being off it, without a restart
#ifdef CAM_TEST_HOOKS  // only in the PC builds, for the tests
  void endAlerts();                 // ends every alert now, without waiting for its hold
  void setNvrGoneMs(uint32_t ms);  // how soon the NVR counts as gone, instead of 3 minutes
#endif

 private:
  // lwIP has 16 sockets. Up to 7 are always taken (4 listening, discovery, ping, the setup page's DNS), 2 are for video,
  // and web connections, event pushes and the webhook share the other 7. Web connections and the webhook together stop
  // at 6, so a push can always start.
  static const int MAX_HTTP = 6;
  static const int MAX_RTSP = 2;  // the NVR and one viewer; each costs a picture-sized buffer plus network buffers
  static const int MAX_UNKNOWN = 8;
  static const int HOOK_QUEUE = 8;

  struct Health {
    bool heard = false;
    uint32_t heardAt = 0;
    uint32_t watchedAt = 0;  // when the hub started expecting reports, so a node dead since before a restart is still noticed
    bool silent = false;
    uint8_t battery = CAM_BATTERY_UNKNOWN;
    bool counterSeen = false;
    uint32_t counter = 0;      // the last report number, used only to ignore resends
    uint32_t counterAt = 0;    // when it arrived
    bool counterActive = false;
    bool reported = false;     // the state the sensor last reported, for the event history
    int8_t rssi = 0;  // signal of the last ESP-NOW packet, in dBm; 0 until one arrives
    bool lowBattery = false;
    int16_t rssiAvg8 = 0;  // smoothed signal, in eighths of a dBm
    uint8_t rssiCount = 0;
    bool weak = false;
    uint32_t refused = 0;  // ESP-NOW messages refused, and why the last one was
    const char* refusal = "";
  };
  struct Slot {
    bool used = false;
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    uint8_t input = 0;
    std::string name;  // from name.txt in the sensor's folder
    CamSensorSettings settings;  // from settings.txt
    CamNodeSettings node;        // from settings.txt in the node's folder, the same for each of its sensors
  };
  struct UnknownSender {  // sent a valid report but has no folder, so its reports are ignored
    bool used = false;
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    uint8_t input = 0;
    int8_t rssi = 0;
    uint32_t heardAt = 0;
    uint32_t messages = 0;
  };
  struct HttpConn {
    int fd = -1;
    bool web = false;  // arrived on the web port rather than the ONVIF port
    uint32_t peer = 0;
    uint32_t openedAt = 0;
    std::string in, out;
    size_t outPos = 0;
    bool answered = false;
    const char* staticBody = nullptr;  // sent after out without being copied into it: flash data, or the borrowed buffer
    size_t staticLen = 0;
    std::string body;  // a reply too large to copy; staticBody points into it
    bool toFrameBuffer = false;  // the body of this picture upload is going into frameData_
    size_t bodyWanted = 0;
    size_t discardLeft = 0;  // body still to read and drop, so a refusal arrives as a reply rather than a reset
    bool pollWaiting = false;  // a PullMessages held until something changes or pollUntil
    uint32_t pollUntil = 0;
    uint32_t pollSub = 0;
    std::string pollHost;
    bool firmwareActive = false;  // the body is going straight to the update, not into memory
    size_t firmwareLeft = 0;
    const std::function<bool(size_t, char*, size_t)>* pull = nullptr;  // reads the body piece by piece instead of staticBody
    // A body of unknown length, read piece by piece until it gives nothing, then ended by closing the connection.
    const std::function<size_t(size_t, char*, size_t)>* stream = nullptr;
  };
  struct RtspConn {
    int fd = -1;
    uint32_t peer = 0;
    uint32_t activityAt = 0;
    std::string in, out;
    size_t outPos = 0;
    bool playing = false;
    bool closeAfterSend = false;
    bool authed = false;
    std::string nonce;
    int channel = 0;
    RtpState rtp;
  };
  struct NotifyIo {
    enum State { IDLE, CONNECTING, SENDING, WAIT_CLOSE } state = IDLE;
    int fd = -1;
    std::string out;
    size_t outPos = 0;
    uint32_t deadline = 0;
    bool hook = false;  // the webhook, rather than an ONVIF event push
  };

  int listenOn(uint16_t port);
  void openDiscovery();
  void acceptHttp(int listenFd, uint32_t now);
  void acceptRtsp(uint32_t now);
  void serviceHttp(HttpConn& c, bool readable, bool writable, uint32_t now);
  void serviceRtsp(RtspConn& c, bool readable, bool writable, uint32_t now);
  void closeHttp(HttpConn& c);
  void closeRtsp(RtspConn& c);
  bool handleHttp(HttpConn& c, uint32_t now);
  std::string handleOnvif(HttpConn& c, const std::string& headers, const std::string& body, uint32_t now, int& status);
  std::string handleSensor(const std::string& target, int& status);
  void rescan();  // rebuilds the sensor table from the folders on storage
  bool sensorFits(const std::string& path) const;  // false when the path would add a sensor beyond the limit
  int sharedSockets() const;  // web connections and event pushes in progress
  int findSlot(const uint8_t mac[6], uint8_t input) const;
  std::string slotName(int id) const;
  const CamFrame& frameToShow(uint32_t now);
  bool loadFrame(const std::string& path);
  bool authorized(const HttpConn& c, const std::string& headers);
  bool knownHost(const std::string& hostHeader) const;
  CamLoginGuard logins_;  // wrong passwords, on the web page, ONVIF and RTSP
  void handleApi(HttpConn& c, const std::string& method, const std::string& target, const std::string& headers, const char* body, size_t bodyLen);
  struct ApiRequest {
    HttpConn& c;
    const std::string& method;
    const std::string& route;
    const std::string& target;
    const char* body;
    size_t bodyLen;
    bool is(const char* m, const char* r) const { return method == m && route == r; }
    std::string form() const { return "?" + std::string(body, bodyLen); }  // so queryParam can read a posted form
  };
  void apiSetup(const ApiRequest& r);
  bool apiPages(const ApiRequest& r);
  bool apiInfo(const ApiRequest& r);
  bool apiSystem(const ApiRequest& r);
  bool apiNetwork(const ApiRequest& r);
  void apiFiles(const ApiRequest& r);
  std::string espNowJson();
  static void sendBody(HttpConn& c, const char* type, const char* data, size_t len, const std::string& extraHeaders = std::string());
  static void reply(HttpConn& c, int status, const char* type, std::string body);
  void shortOfMemory(HttpConn& c);
  int streamCount() const;
  bool roomFor(size_t bytes) const { return !cfg_.canAllocate || cfg_.canAllocate(bytes); }
  bool hasHeadroom() const { return !cfg_.hasHeadroom || cfg_.hasHeadroom(); }
  std::string treeJson();
  bool applyReport(int id, const SensorReport& report, uint32_t now);
  void logEvent(int id, const SensorReport& report, bool delayed, CamEventKind kind = CAM_EVENT_REPORT);
  void addEvent(const std::string& sensor, const std::string& text, const SensorReport& report, bool delayed, CamEventKind kind, uint32_t offS = CAM_AGE_UNKNOWN);
  void hubEvent(CamEventKind kind, const std::string& detail, uint32_t offS, const std::string& message);
  bool nvrListening(uint32_t now) const;
  bool roomBesideHistory(size_t bytes) const;  // whether a picture of this size still leaves the event history its room
  size_t historyRoom() const;                  // the storage the event history may still need
  void checkNvr(uint32_t now);
  void noteHubStart(uint32_t now);
  void startAlert(int id, bool delayed, uint32_t now, bool replay = false);  // an alert that is a new NVR record, to be checked as delivered
  void healthAlert(int id, CamEventKind kind, uint32_t now);
  bool persist();
  bool downloadingHistory() const;
  void saveWaiting();
  void loadWaiting();
  void servicePolls(uint32_t now);
  void feedFirmware(HttpConn& c, const char* data, size_t n, uint32_t now);
  void finishFirmware(HttpConn& c, bool ok);
  void noteUnknown(const uint8_t mac[6], uint8_t input, int8_t rssi);
  void serveSnapshot(HttpConn& c);
  void serviceDelayed(uint32_t now);
  std::string statusJson(uint32_t now);
  std::string eventsJson();
  void handleRtspRequest(RtspConn& c, const std::string& text, uint32_t now);
  void flushRtsp(RtspConn& c, uint32_t now);
  void startNotify(int i, uint32_t now);
  void serviceNotify(NotifyIo& n, bool readable, bool writable, uint32_t now);
  void endNotify(NotifyIo& n, const char* result = "");
  void noteSignal(int id, int8_t rssi);
  uint32_t holdFor(int id) const;
  void queueDelayed(int id, const SensorReport& r, uint32_t now);
  void interruptPastEvent(uint32_t now);
  bool quietNow(int id) const;
  void notifyHook(int id, CamEventKind kind, bool delayed);
  void notifyHubHook(CamEventKind kind, const std::string& detail, const std::string& message);
  void queueHook(const std::string& message, const std::string& json);
  void startHook(uint32_t now);
  std::string webhookJson();
  bool apiSensors(const ApiRequest& r);
  void serviceDiscovery();
  void pushFrames(uint32_t now);

  CamConfig cfg_;
  std::function<std::string()> localIp_;
  std::function<uint32_t()> millis_;
  SensorBoard sensors_;
  MotionSignal signal_;
  SubTable subs_;
  int onvifListen_ = -1, webListen_ = -1, captiveListen_ = -1, rtspListen_ = -1, udp_ = -1;
  HttpConn http_[MAX_HTTP];
  RtspConn rtsp_[MAX_RTSP];
  NotifyIo notify_[SubTable::SIZE];
  uint32_t nextFrameAt_ = 0;
  uint32_t messageNo_ = 1;
  uint32_t lastGoodWait_ = 0;
  uint32_t waitErrors_ = 0;
  int lastWaitErrno_ = 0;
  uint32_t accepted_ = 0;
  uint32_t refused_ = 0;
  CamStorage storage_;
  CamEventLog log_{storage_};  // after storage_, which it uses
  std::string confirmations_;  // waiting to be sent once the reports are saved: 6 bytes of MAC address, then the message
  std::vector<Slot> slots_;
  std::string frameKey_;  // what frame_ currently shows; empty forces a reload
  std::string frameFile_;  // the file behind frame_; empty means a built-in picture
  std::string unsupported_[6];  // the ONVIF requests the hub could not answer, shown on the ONVIF page
  uint32_t liveSeq_ = 0;  // changes whenever the picture being sent changes, so the Live page fetches only new ones
  const CamFrame* frame_ = nullptr;
  std::string frameData_;
  std::string spareOut_[MAX_RTSP];  // stream buffers reserved at boot, because a picture-sized allocation can fail once the hub is busy
  // An idle stream buffer lent to one reply at a time. A picture-sized allocation can fail and restart the hub,
  // and a third reserved buffer would leave the network stack too little memory to send video.
  std::string* fileOut_ = nullptr;
  HttpConn* fileOutOwner_ = nullptr;
  std::string* borrowFileOut(HttpConn& c);
  HttpConn* firmwareOwner_ = nullptr;
  HttpConn* uploading_ = nullptr;  // the upload borrowing frameData_, during which the built-in pictures are shown
  CamNal frameNals_[3];
  CamFrame loadedFrame_{frameNals_, 3};
  std::vector<Health> health_;
  UnknownSender unknown_[MAX_UNKNOWN];
  bool waitingDirty_ = false;  // the alerts waiting for an NVR record changed since they were saved
  bool waitingSaveFailed_ = false;
  CamDelayedQueue delayed_;
  int playingId_ = 0;   // the sensor whose past event is playing, and that past event
  CamDelayed playing_;
  CamDeliveryWatch delivery_;  // whether each alert reached the NVR, and those it missed
  bool pendingChanged_ = false;  // a new alert awaits its check; saved with the report that started it
  uint32_t nextDelayedAt_ = 0;
  bool delayedGap_ = false;
  NotifyIo hook_;
  std::string hookQueue_[HOOK_QUEUE];
  int hookCount_ = 0;
  int hookHead_ = 0;
  uint32_t hookDropped_ = 0;
  std::string hookResult_;  // how the last message went; empty before the first
  time_t hookResultAt_ = 0;
  std::function<size_t(size_t, char*, size_t)> csvRead_;  // streams the whole history for a download
  uint32_t nvrSeenAt_ = 0;  // when the NVR was last listening, or the hub started
  bool nvrGone_ = false;
  uint32_t nvrGoneMs_ = CAM_NVR_GONE_MS;  // a test may shorten it
  uint32_t beganAt_ = 0;
  bool networkSeen_ = false;
  bool startNoted_ = false;
  time_t lastAlive_ = 0;  // the last time the previous run knew it was running, from storage; 0 when unknown
  uint32_t aliveAt_ = 0;
  bool aliveWritten_ = false;
  void writeAlive(uint32_t now);
};
