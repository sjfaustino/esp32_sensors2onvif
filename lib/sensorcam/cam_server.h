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
#include "cam_pairing.h"
#include "cam_rtp.h"
#include "cam_sensors.h"
#include "cam_soap.h"
#include "cam_storage.h"
#include "cam_subs.h"
#include "cam_timing.h"

class SensorCamServer : private CamAlarmOutside {
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
  void saveNow() { alarm_.saveNow(); }                              // writes any events not yet saved; call before a restart
  void networkLost(uint32_t seconds) { alarm_.networkLost(seconds); }  // call on rejoining Wi-Fi after being off it, without a restart
  bool updating() const { return firmwareOwner_ != nullptr; }          // a firmware upload is under way
  bool nvrListeningNow() const { return nvrListening(millis_()); }     // see nvrListening
  // Times memory ran out and only that piece of work was dropped.
  uint32_t memoryFailures() const { return memoryFailures_ + alarm_.memoryFailures(); }
  bool armed() const { return alarm_.armed(); }
  CamArmMode armMode() const { return alarm_.armMode(); }
  // See CamAlarm::setArmMode and CamAlarm::testAlert.
  bool setArmMode(CamArmMode mode, const std::string& from, uint32_t minutes = 0) { return alarm_.setArmMode(mode, from, minutes); }
  bool testAlert(int id) { return alarm_.testAlert(id); }
#ifdef CAM_TEST_HOOKS                                                 // only in the PC builds, for the tests
  void endAlerts() { alarm_.endAlerts(); }                            // ends every alert now, without waiting for its hold
  void setNvrGoneMs(uint32_t ms) { alarm_.setNvrGoneMs(ms); }         // how soon the NVR counts as gone, instead of 3 minutes
  void setMinuteMs(uint32_t ms) { alarm_.setMinuteMs(ms); }           // how long a minute of a timed disarm lasts
  void setBusyAlerts(int n) { alarm_.setBusyAlerts(n); }              // how many alerts within an hour count as firing often
  void setOverdueMs(uint32_t ms) { alarm_.setOverdueMs(ms); }         // how late a node's report is overdue, instead of from its report time; 0 for that
#endif

 private:
  // lwIP has 16 sockets. Up to 7 are always taken (4 listening, discovery, ping, the setup page's DNS), 2 are for video,
  // and web connections, event pushes, the webhook and MQTT share the other 7. All but the pushes together stop at 6,
  // so a push can always start.
  static const int MAX_HTTP = 6;
  static const int MAX_RTSP = 2;  // the NVR and one viewer; each costs a packet buffer plus network buffers

  using Health = CamAlarm::Health;
  using Node = CamAlarm::Node;
  using Slot = CamAlarm::Slot;
  using UnknownSender = CamAlarm::UnknownSender;
  struct PairRequest {  // a node that said hello while pairing was open, waiting for approval
    bool used = false;
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    uint8_t inputs = 1;
    uint8_t publicKey[CAM_X25519_LEN] = {0};
    uint32_t heardAt = 0;
    int8_t rssi = 0;
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
    std::string body;            // a reply too large to copy; staticBody points into it
    bool toFrameBuffer = false;  // the body of this picture upload is going into frameData_
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
  struct RtspConn {
    int fd = -1;
    uint32_t peer = 0;
    uint32_t activityAt = 0;
    std::string in, out;  // out holds the RTSP replies; video goes through packet
    size_t outPos = 0;
    RtpSend video;                   // the frame being sent, packet by packet
    uint8_t packet[RTP_PACKET_MAX];  // the packet being sent, which may go out in parts
    size_t packetLen = 0, packetPos = 0;
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
  int sharedSockets() const;                       // web connections and event pushes in progress
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
  int streamCount() const;
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
  void handleRtspRequest(RtspConn& c, const std::string& text, uint32_t now);
  void flushRtsp(RtspConn& c, uint32_t now);
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

  // ---- pairing nodes: cam_server_pairing.cpp ----
  static const int MAX_PAIR_REQUESTS = 8;
  void helloPacket(const uint8_t mac[6], const uint8_t* data, int len, int8_t rssi);
  void firmwareAskPacket(const uint8_t mac[6], const uint8_t* data, int len);
  HttpConn* nodeImageOwner_ = nullptr;  // the upload of a node firmware file under way
  const CamNodeImage* imageFor(uint16_t chip) const { return nodeImages_.forChip(chip); }
  void finishNodeImage(HttpConn& c, bool ok);
  void startNodeImage(HttpConn& c, size_t length);
  void dropNodeImage();  // an upload abandoned partway through
  bool apiNodeImages(const ApiRequest& r);
  bool pairingOpen(uint32_t now) const { return pairingOpen_ && (int32_t)(pairingUntil_ - now) > 0; }
  bool apiPairing(const ApiRequest& r);
  std::string pairingJson();
  bool approveNode(const uint8_t mac[6], std::string& error);
  bool pairingReady_ = false;
  uint8_t hubPrivate_[CAM_X25519_LEN] = {0}, hubPublic_[CAM_X25519_LEN] = {0};
  bool pairingOpen_ = false;
  uint32_t pairingUntil_ = 0;
  PairRequest pairRequests_[MAX_PAIR_REQUESTS];

  // ---- MQTT, for Home Assistant: cam_server_mqtt.cpp ----
  struct MqttShown {  // a sensor's state as last published, so only a change is sent
    bool told = false;
    bool alert = false, silent = false, low = false, weak = false;
    uint8_t battery = CAM_BATTERY_UNKNOWN;
    int8_t rssi = 0;
    uint32_t at = 0;
  };
  struct MqttTold {  // a sensor Home Assistant was told of, so one deleted or replaced is taken away again
    bool used = false;
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    uint8_t input = 0;
  };
  struct MqttIo {
    enum State { OFF, CONNECTING, WAIT_CONNACK, READY } state = OFF;
    int fd = -1;
    std::string out;
    size_t outPos = 0;
    uint8_t in[64];
    size_t inLen = 0;
    size_t skip = 0;  // bytes of a packet too large for in, read and dropped
    uint32_t deadline = 0, sentAt = 0, heardAt = 0;
    uint16_t nextId = 1;
    uint16_t ackId = 0;     // the event sent and not yet acknowledged; 0 for none
    uint32_t ackEvent = 0;  // its number, counting the events added to the log since the start
    uint32_t ackSince = 0;
    bool online = false;
    int discovery = 0;  // the next discovery message; -1 once all are sent
    bool hubTold = false, hubAlert = false, hubNvr = false, hubArmed = false, hubHome = false;
    int64_t hubUntil = 0;
    bool oldSwitchGone = false;  // the Armed switch of earlier firmware, taken away from Home Assistant
    int subscribed = 0;          // topics subscribed to: the alarm panel's, then the test buttons'
    bool healthTold = false;
    uint32_t healthAt = 0;
  };
  void serviceMqtt(bool readable, bool writable, uint32_t now);
  void mqttHousekeeping(uint32_t now);
  void startMqtt(uint32_t now);
  void endMqtt(const char* why, uint32_t now);
  void mqttPacket(const uint8_t* p, size_t len, uint32_t now);
  bool mqttNext(uint32_t now, std::string& out);  // the next message to send, if any
  bool mqttDiscoveryStep(int step, std::string& out);
  bool mqttSensorState(int i, bool alertsOnly, uint32_t now, std::string& out);
  bool mqttEvent(std::string& out);
  std::string mqttBase() const;  // the hub's topics start with this
  std::string mqttJson();
  std::string mqttState() const;  // as the page shows it
  std::string overviewJson(uint32_t now);
  MqttIo mqtt_;
  MqttShown mqttShown_[CAM_MAX_SENSORS];
  MqttTold mqttTold_[CAM_MAX_SENSORS];
  uint32_t mqttEventsSent_ = 0;  // the events published, counting as CamEventLog::added does
  uint32_t mqttEventsLost_ = 0;  // more waited than the log keeps in memory
  uint32_t mqttRetryAt_ = 0;
  bool mqttWaiting_ = false;  // a connection failed, and the next waits until mqttRetryAt_
  std::string mqttResult_ = "not set up";
  time_t mqttConnectedAt_ = 0;
  void pushFrames(uint32_t now);

  CamConfig cfg_;
  CamChip* chip_;  // the board's parts, or ones with every default for a part left out
  CamRadio* radio_;
  CamStore* store_;
  std::function<std::string()> localIp_;
  std::function<uint32_t()> millis_;
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
  CamNodeImages nodeImages_{storage_};
  CamAlarm alarm_;  // after cfg_, the parts, storage_ and nodeImages_, which it uses
  std::string frameKey_;        // what frame_ currently shows; empty forces a reload
  std::string frameFile_;       // the file behind frame_; empty means a built-in picture
  std::string unsupported_[6];  // the ONVIF requests the hub could not answer, shown on the ONVIF page
  uint32_t liveSeq_ = 0;        // changes whenever the picture being sent changes, so the Live page fetches only new ones
  const CamFrame* frame_ = nullptr;
  std::string frameData_;    // the picture loaded from storage, in one buffer reserved at boot
  uint32_t pictureGen_ = 0;  // changes whenever frameData_ is about to be overwritten, so a reply reading it stops
  void releasePicture();     // before frameData_ is overwritten: nothing may still be reading the old picture
  bool sendFile(HttpConn& c, const std::string& path, const char* type);  // false when there is no such file
  HttpConn* firmwareOwner_ = nullptr;
  HttpConn* uploading_ = nullptr;  // the upload borrowing frameData_, during which the built-in pictures are shown
  CamNal frameNals_[3];
  CamFrame loadedFrame_{frameNals_, 3};
  NotifyIo hook_;
  CamHookQueue hooks_;
  std::function<size_t(size_t, char*, size_t)> csvRead_;    // streams the whole history for a download
  std::function<bool(size_t, char*, size_t)> crashReader_;  // reads the chip's crash report for a download
  bool dailySent_ = false;  // and when, so the minute it is due sends one
  uint32_t dailySentAt_ = 0;
};
