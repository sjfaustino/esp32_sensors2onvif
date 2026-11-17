// ONVIF device, RTSP stream, sensor input and WS-Discovery in one non-blocking loop.
#pragma once
#include <stdint.h>
#include <time.h>
#include <functional>
#include <string>
#include <vector>
#include "cam_alarm.h"
#include "cam_auth.h"
#include "cam_config.h"
#include "cam_delivery.h"
#include "cam_events.h"
#include "cam_hook_queue.h"
#include "cam_node_images.h"
#include "cam_json.h"
#include "cam_mqtt_client.h"
#include "cam_nodes.h"
#include "cam_video.h"
#include "cam_pairing.h"
#include "cam_rtp.h"
#include "cam_sensors.h"
#include "cam_soap.h"
#include "cam_storage.h"
#include "cam_subs.h"
#include "cam_timing.h"

class SensorCamServer : private CamAlarmOutside, private CamMqttOutside {
 public:
  SensorCamServer(const CamConfig& cfg, std::function<std::string()> localIp, std::function<uint32_t()> millisFn);
  bool begin();
  void poll(int maxWaitMs);  // call continuously
  void networkUp();          // call after each Wi-Fi (re)connect
  // Milliseconds since the socket wait last worked. A large value means the server has stopped serving.
  uint32_t stalledMs(uint32_t now) const { return now - lastGoodWait_; }
  std::string debugLine();                                                                 // counters for the serial heartbeat
  void setOnvifPassword(const std::string& password) { cfg_.onvifPassword = password; }    // for the PC tests
  void espNowPacket(const uint8_t mac[6], const uint8_t* data, int len, int8_t rssi = 0);  // a raw ESP-NOW message; call from the same task as poll()
  void saveNow() { alarm_.saveNow(); }                                                     // writes any events not yet saved; call before a restart
  void networkLost(uint32_t seconds) { alarm_.networkLost(seconds); }  // call on rejoining Wi-Fi after being off it, without a restart
  bool updating() const { return firmwareOwner_ != nullptr; }          // a firmware upload is under way
  bool nvrListeningNow() const { return nvrListening(millis_()); }     // see nvrListening
  // Times memory ran out and only that piece of work was dropped.
  uint32_t memoryFailures() const override { return memoryFailures_ + alarm_.memoryFailures() + video_.memoryFailures(); }
  bool armed() const { return alarm_.armed(); }
  CamArmMode armMode() const { return alarm_.armMode(); }
  // See CamAlarm::setArmMode and CamAlarm::testAlert.
  bool setArmMode(CamArmMode mode, const std::string& from, uint32_t minutes = 0) { return alarm_.setArmMode(mode, from, minutes); }
  bool testAlert(int id) { return alarm_.testAlert(id); }
#ifdef CAM_TEST_HOOKS                                          // only in the PC builds, for the tests
  void endAlerts() { alarm_.endAlerts(); }                     // ends every alert now, without waiting for its hold
  void setNvrGoneMs(uint32_t ms) { alarm_.setNvrGoneMs(ms); }  // how soon the NVR counts as gone, instead of 3 minutes
  void setMinuteMs(uint32_t ms) { alarm_.setMinuteMs(ms); }    // how long a minute of a timed disarm lasts
  void setBusyAlerts(int n) { alarm_.setBusyAlerts(n); }       // how many alerts within an hour count as firing often
  void setOverdueMs(uint32_t ms) { alarm_.setOverdueMs(ms); }  // how late a node's report is overdue, instead of from its report time; 0 for that
#endif

 private:
  // lwIP has 16 sockets. Up to 7 are always taken (4 listening, discovery, ping, the setup page's DNS), 2 are for video,
  // and web connections, event pushes, the webhook and MQTT share the other 7. All but the pushes together stop at 6,
  // so a push can always start.
  static const int MAX_HTTP = 6;

  using Health = CamAlarm::Health;
  using Node = CamAlarm::Node;
  using Slot = CamAlarm::Slot;
  using UnknownSender = CamAlarm::UnknownSender;
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
    std::string body;            // a reply too large to copy; staticBody points into it
    bool toFrameBuffer = false;  // the body of this picture upload is going into the picture buffer (CamVideo::lend)
    size_t bodyWanted = 0;
    size_t discardLeft = 0;    // body still to read and drop, so a refusal arrives as a reply rather than a reset
    bool pollWaiting = false;  // a PullMessages held until something changes or pollUntil
    uint32_t pollUntil = 0;
    uint32_t pollSub = 0;
    std::string pollHost;
    // The subscription whose last answered state this reply changed, and the state before, put back if memory runs out
    // before the reply is ready: otherwise the NVR would never hear of that change.
    uint32_t undoSub = 0;
    int undoState = 0;
    bool firmwareActive = false;  // the body is going straight to the update, not into memory
    bool nodeImage = false;       // to a node firmware file instead
    size_t firmwareLeft = 0;
    const std::function<bool(size_t, char*, size_t)>* pull = nullptr;  // reads the body piece by piece instead of staticBody
    std::function<bool(size_t, char*, size_t)> reader;                 // this reply's own piece reader, a file or the picture; pull points at it
    // A body of unknown length, read piece by piece until it gives nothing, then ended by closing the connection.
    const std::function<size_t(size_t, char*, size_t)>* stream = nullptr;
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
  void serviceHttp(HttpConn& c, bool readable, bool writable, uint32_t now);
  void closeHttp(HttpConn& c);
  bool handleHttp(HttpConn& c, uint32_t now);
  std::string handleOnvif(HttpConn& c, const std::string& headers, const std::string& body, uint32_t now, int& status);
  std::string handleSensor(const std::string& target, int& status);
  int sharedSockets() const;  // web connections and event pushes in progress
  bool authorized(const HttpConn& c, const std::string& headers);
  bool knownHost(const std::string& hostHeader) const;
  CamLoginGuard logins_;  // wrong passwords, on the web page, ONVIF and RTSP
  void handleApi(HttpConn& c, const std::string& method, const std::string& target, const std::string& headers, const char* body, size_t bodyLen);
  struct ApiRequest {
    HttpConn& c;
    const std::string& method;
    const std::string& route;
    const std::string& target;
    const std::string& headers;
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
  static void reply(HttpConn& c, int status, const char* type, std::string body, const std::string& extraHeaders = std::string());
  void shortOfMemory(HttpConn& c);
  void undoPull(HttpConn& c);       // see HttpConn::undoSub
  void housekeeping(uint32_t now);  // alerts, saving, the NVR and video: the part of poll() before waiting
  void pollOnce(int maxWaitMs);     // waits for the network, then serves the connections
  uint32_t memoryFailures_ = 0;
  bool roomFor(size_t bytes) const { return chip_->canAllocate(bytes); }
  bool hasHeadroom() const { return chip_->hasHeadroom(); }
  std::string treeJson();
  bool nvrListening(uint32_t now) const override;
  bool roomBesideHistory(size_t bytes) const;  // whether a picture of this size still leaves the event history its room
  size_t historyRoom() const;                  // the storage the event history may still need
  bool downloadingHistory() const override;
  void servicePolls(uint32_t now);
  void feedFirmware(HttpConn& c, const char* data, size_t n, uint32_t now);
  void finishFirmware(HttpConn& c, bool ok);
  void serveSnapshot(HttpConn& c);
  std::string statusJson(uint32_t now);
  std::string eventsJson(bool* whole = nullptr);  // whole: false when the list was trimmed for lack of memory
  std::string eventsTag() const;                  // changes whenever the Events page's list would, so an unchanged list is not built again
  void startNotify(int i, uint32_t now);
  void serviceNotify(NotifyIo& n, bool readable, bool writable, uint32_t now);
  void endNotify(NotifyIo& n, const char* result = "");
  void dailyCheck(uint32_t now);
  void notifyHook(int id, CamEventKind kind, bool delayed) override;
  void notifyHubHook(CamEventKind kind, const std::string& detail, const std::string& message) override;
  void sensorReset(int id) override;
  void sensorsRescanned(bool listChanged) override;
  void queueHook(const std::string& message, const std::string& json);
  void startHook(uint32_t now);
  std::string webhookJson();
  bool apiSensors(const ApiRequest& r);
  void serviceDiscovery();

  // ---- the web side of CamNodes: cam_server_nodes.cpp ----
  HttpConn* nodeImageOwner_ = nullptr;  // the upload of a node firmware file under way
  void finishNodeImage(HttpConn& c, bool ok);
  void startNodeImage(HttpConn& c, size_t length);
  void dropNodeImage();  // an upload abandoned partway through
  bool apiNodeImages(const ApiRequest& r);
  bool apiPairing(const ApiRequest& r);

  std::string overviewJson(uint32_t now);

  CamConfig cfg_;
  CamChip* chip_;  // the board's parts, or ones with every default for a part left out
  CamRadio* radio_;
  CamStore* store_;
  std::function<std::string()> localIp_;
  std::function<uint32_t()> millis_;
  MotionSignal signal_;
  SubTable subs_;
  int onvifListen_ = -1, webListen_ = -1, captiveListen_ = -1, udp_ = -1;
  HttpConn http_[MAX_HTTP];
  NotifyIo notify_[SubTable::SIZE];
  uint32_t messageNo_ = 1;
  uint32_t lastGoodWait_ = 0;
  uint32_t waitErrors_ = 0;
  int lastWaitErrno_ = 0;
  uint32_t accepted_ = 0;
  uint32_t refused_ = 0;
  CamStorage storage_;
  CamNodes nodes_;  // before alarm_, which reads its firmware files
  CamAlarm alarm_;  // after cfg_, the parts, storage_ and nodes_, which it uses
  CamMqttClient mqtt_;
  CamVideo video_;
  std::string unsupported_[6];                                            // the ONVIF requests the hub could not answer, shown on the ONVIF page
  bool sendFile(HttpConn& c, const std::string& path, const char* type);  // false when there is no such file
  HttpConn* firmwareOwner_ = nullptr;
  HttpConn* uploading_ = nullptr;  // the upload borrowing the picture buffer (CamVideo::lend)
  NotifyIo hook_;
  CamHookQueue hooks_;
  std::function<size_t(size_t, char*, size_t)> csvRead_;    // streams the whole history for a download
  std::function<bool(size_t, char*, size_t)> crashReader_;  // reads the chip's crash report for a download
  bool dailySent_ = false;                                  // and when, so the minute it is due sends one
  uint32_t dailySentAt_ = 0;
};
