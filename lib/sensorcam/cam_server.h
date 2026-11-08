// ONVIF device, RTSP stream, sensor input and WS-Discovery in one non-blocking loop.
#pragma once
#include <stdint.h>
#include <functional>
#include <string>
#include "cam_rtp.h"
#include "cam_sensors.h"
#include "cam_subs.h"

struct CamConfig {
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
  bool sensorInput(int id, bool active);  // call from the same task as poll()

 private:
  static const int MAX_HTTP = 6;
  static const int MAX_RTSP = 3;

  struct HttpConn {
    int fd = -1;
    uint32_t peer = 0;
    uint32_t openedAt = 0;
    std::string in, out;
    size_t outPos = 0;
    bool answered = false;
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
  std::string statusJson(uint32_t now);
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
};
