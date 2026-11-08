// ONVIF device, RTSP stream, sensor input and WS-Discovery in one non-blocking loop.
#pragma once
#include <stdint.h>
#include <time.h>
#include <functional>
#include <string>
#include <vector>
#include "cam_rtp.h"
#include "cam_sensors.h"
#include "cam_storage.h"
#include "cam_subs.h"

const int CAM_MAX_SENSORS = 32;

struct CamConfig {
  std::string fsRoot = "/littlefs";
  std::string webPassword;  // when set, the file manager requires Basic auth as "admin"
  std::function<void(uint32_t& used, uint32_t& total)> fsUsage;
  std::function<bool(size_t)> canAllocate;  // lets large requests be refused before they exhaust RAM
  uint16_t onvifPort = 8899;
  uint16_t rtspPort = 8554;
  uint16_t discoveryPort = 3702;
  bool loopbackOnly = false;  // PC test build: nothing leaves this machine
  uint32_t holdMs = 10000;
  std::string sensorToken;  // when set, /sensor requires token=<value>
  std::string serial = "000000000000";
};

class SensorCamServer {
 public:
  SensorCamServer(const CamConfig& cfg, std::function<std::string()> localIp, std::function<uint32_t()> millisFn);
  bool begin();
  void poll(int maxWaitMs);  // call continuously
  void networkUp();          // call after each Wi-Fi (re)connect
  void espNowInput(const uint8_t mac[6], const SensorReport& report);  // call from the same task as poll()

 private:
  static const int MAX_HTTP = 6;
  static const int MAX_RTSP = 3;
  static const int EVENT_LOG_SIZE = 64;
  static const int DELAYED_QUEUE_SIZE = 16;

  struct Health {
    bool heard = false;
    uint32_t heardAt = 0;
    bool silent = false;
    uint8_t battery = CAM_BATTERY_UNKNOWN;
    bool counterSeen = false;
    uint32_t counter = 0;
  };
  struct Slot {
    bool used = false;
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    uint8_t input = 0;
  };
  struct CamEvent {
    time_t received = 0;
    uint32_t age = 0;
    char sensor[22] = "";  // AA-BB-CC-DD-EE-FF/255
    bool active = false;
    bool delayed = false;
    uint8_t battery = CAM_BATTERY_UNKNOWN;
    uint8_t targetCount = 0;
    SensorTarget targets[CAM_MAX_TARGETS];
  };
  struct DelayedAlert {
    uint8_t sensor = 0;
    uint8_t direction = 0;
  };

  struct HttpConn {
    int fd = -1;
    uint32_t peer = 0;
    uint32_t openedAt = 0;
    std::string in, out;
    size_t outPos = 0;
    bool answered = false;
    const char* staticBody = nullptr;  // sent after out, straight from flash, so a large page never sits in RAM
    size_t staticLen = 0;
  };
  struct RtspConn {
    int fd = -1;
    uint32_t peer = 0;
    uint32_t activityAt = 0;
    std::string in, out;
    size_t outPos = 0;
    bool playing = false;
    bool closeAfterSend = false;
    int channel = 0;
    RtpState rtp;
  };
  struct NotifyIo {
    enum State { IDLE, CONNECTING, SENDING, WAIT_CLOSE } state = IDLE;
    int fd = -1;
    std::string out;
    size_t outPos = 0;
    uint32_t deadline = 0;
  };

  int listenOn(uint16_t port);
  void openDiscovery();
  void acceptHttp(uint32_t now);
  void acceptRtsp(uint32_t now);
  void serviceHttp(HttpConn& c, bool readable, bool writable, uint32_t now);
  void serviceRtsp(RtspConn& c, bool readable, bool writable, uint32_t now);
  void closeHttp(HttpConn& c);
  void closeRtsp(RtspConn& c);
  bool handleHttp(HttpConn& c, uint32_t now);
  std::string handleOnvif(HttpConn& c, const std::string& headers, const std::string& body, uint32_t now, int& status);
  std::string handleSensor(const std::string& target, int& status);
  void rescan();  // rebuilds the sensor table from the folders on storage
  int findSlot(const uint8_t mac[6], uint8_t input) const;
  std::string slotName(int id) const;
  const CamFrame& frameToShow(uint32_t now);
  bool loadFrame(const std::string& path);
  bool authorized(const std::string& headers) const;
  void handleApi(HttpConn& c, const std::string& method, const std::string& target, const std::string& headers, const char* body, size_t bodyLen);
  std::string treeJson();
  bool applyReport(int id, const SensorReport& report, uint32_t now);
  void logEvent(int id, const SensorReport& report, bool delayed);
  void serviceDelayed(uint32_t now);
  std::string statusJson(uint32_t now);
  std::string eventsJson();
  void handleRtspRequest(RtspConn& c, const std::string& text, uint32_t now);
  void flushRtsp(RtspConn& c, uint32_t now);
  void startNotify(int i, uint32_t now);
  void serviceNotify(NotifyIo& n, bool readable, bool writable, uint32_t now);
  void endNotify(NotifyIo& n);
  void serviceDiscovery();
  void pushFrames(uint32_t now);

  CamConfig cfg_;
  std::function<std::string()> localIp_;
  std::function<uint32_t()> millis_;
  SensorBoard sensors_;
  MotionSignal signal_;
  SubTable subs_;
  int httpListen_ = -1, rtspListen_ = -1, udp_ = -1;
  HttpConn http_[MAX_HTTP];
  RtspConn rtsp_[MAX_RTSP];
  NotifyIo notify_[SubTable::SIZE];
  uint32_t nextFrameAt_ = 0;
  uint32_t messageNo_ = 1;
  CamStorage storage_;
  std::vector<Slot> slots_;
  std::string frameKey_;  // what frame_ currently shows; empty forces a reload
  const CamFrame* frame_ = nullptr;
  std::string frameData_;
  CamNal frameNals_[3];
  CamFrame loadedFrame_{frameNals_, 3};
  std::vector<Health> health_;
  CamEvent events_[EVENT_LOG_SIZE];
  int eventCount_ = 0;
  int eventNext_ = 0;
  DelayedAlert delayed_[DELAYED_QUEUE_SIZE];
  int delayedCount_ = 0;
  int delayedHead_ = 0;
  uint32_t nextDelayedAt_ = 0;
  bool delayedGap_ = false;
};
