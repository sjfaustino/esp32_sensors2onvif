// The hub's HTTP side: the ONVIF requests on the ONVIF port, and on the web port the pages, their API, sensor reports,
// snapshots, file and firmware uploads, and the setup network's pages. SensorCamServer owns one, waits on its sockets,
// and gives it the hub's other parts, which the API shows and changes.
#pragma once
#include <stdint.h>
#include <functional>
#include <string>
#include "cam_alarm.h"
#include "cam_auth.h"
#include "cam_config.h"
#include "cam_json.h"
#include "cam_mqtt_client.h"
#include "cam_nodes.h"
#include "cam_onvif_events.h"
#include "cam_storage.h"
#include "cam_video.h"
#include "cam_webhook.h"

// The parts the web side serves, all owned by SensorCamServer. cfg is the server's, which the API changes as settings are
// saved; memoryFailures is the hub's total, for the pages.
struct CamWebParts {
  CamConfig& cfg;
  CamChip& chip;
  CamRadio& radio;
  CamStore& store;
  CamStorage& storage;
  CamLoginGuard& logins;
  CamNodes& nodes;
  CamAlarm& alarm;
  CamMqttClient& mqtt;
  CamOnvifEvents& events;
  CamVideo& video;
  CamWebhookSender& webhook;
  std::function<std::string()> localIp;
  std::function<uint32_t()> millis;
  std::function<uint32_t()> memoryFailures;
};

class CamWeb {
 public:
  // lwIP has 16 sockets. Up to 7 are always taken (4 listening, discovery, ping, the setup page's DNS), 2 are for video,
  // and web connections, event pushes, the webhook and MQTT share the other 7. All but the pushes together stop at 6,
  // so a push can always start.
  static const int MAX_HTTP = 6;

  explicit CamWeb(const CamWebParts& parts);
  bool begin();  // opens the ONVIF, web and setup ports; false when the ONVIF or web port could not be opened
  void watch(fd_set& rd, fd_set& wr, int& maxFd);
  void serve(fd_set& rd, fd_set& wr, uint32_t now);
  void servicePolls(uint32_t now);                             // answers each held PullMessages once there is news or its wait is over
  int sharedSockets() const;                                   // web connections, event pushes, the webhook and MQTT
  bool updating() const { return firmwareOwner_ != nullptr; }  // a firmware upload is under way
  bool downloadingHistory() const;
  int openCount() const;  // web connections open
  uint32_t accepted() const { return accepted_; }
  uint32_t refused() const { return refused_; }
  uint32_t memoryFailuresHere() const { return memoryFailures_; }

 private:
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

  // ---- connections and requests: cam_web_server.cpp ----
  void acceptHttp(int listenFd, uint32_t now);
  void serviceHttp(HttpConn& c, bool readable, bool writable, uint32_t now);
  void closeHttp(HttpConn& c);
  bool handleHttp(HttpConn& c, uint32_t now);
  std::string handleOnvif(HttpConn& c, const std::string& headers, const std::string& body, uint32_t now, int& status);
  std::string handleSensor(const std::string& target, int& status);
  bool authorized(const HttpConn& c, const std::string& headers);
  bool knownHost(const std::string& hostHeader) const;
  static void sendBody(HttpConn& c, const char* type, const char* data, size_t len, const std::string& extraHeaders = std::string());
  static void reply(HttpConn& c, int status, const char* type, std::string body, const std::string& extraHeaders = std::string());
  bool sendFile(HttpConn& c, const std::string& path, const char* type);  // false when there is no such file
  void shortOfMemory(HttpConn& c);
  void undoPull(HttpConn& c);  // see HttpConn::undoSub
  void feedFirmware(HttpConn& c, const char* data, size_t n, uint32_t now);
  void finishFirmware(HttpConn& c, bool ok);
  void serveSnapshot(HttpConn& c);

  // ---- the API: cam_web_api.cpp ----
  void handleApi(HttpConn& c, const std::string& method, const std::string& target, const std::string& headers, const char* body, size_t bodyLen);
  void apiSetup(const ApiRequest& r);
  bool apiPages(const ApiRequest& r);
  bool apiInfo(const ApiRequest& r);
  bool apiSystem(const ApiRequest& r);
  bool apiNetwork(const ApiRequest& r);
  bool apiSensors(const ApiRequest& r);
  void apiFiles(const ApiRequest& r);
  std::string espNowJson();
  std::string treeJson();
  bool roomBesideHistory(size_t bytes) const;  // whether a picture of this size still leaves the event history its room
  size_t historyRoom() const;                  // the storage the event history may still need

  // ---- what the Overview, Events and status pages read: cam_web_views.cpp ----
  std::string overviewJson(uint32_t now);
  std::string statusJson(uint32_t now);
  std::string eventsJson(bool* whole = nullptr);  // whole: false when the list was trimmed for lack of memory
  std::string eventsTag() const;                  // changes whenever the Events page's list would, so an unchanged list is not built again

  // ---- pairing and node firmware files: cam_web_nodes.cpp ----
  void finishNodeImage(HttpConn& c, bool ok);
  void startNodeImage(HttpConn& c, size_t length);
  void dropNodeImage();  // an upload abandoned partway through
  bool apiNodeImages(const ApiRequest& r);
  bool apiPairing(const ApiRequest& r);

  bool roomFor(size_t bytes) const { return chip_->canAllocate(bytes); }
  bool hasHeadroom() const { return chip_->hasHeadroom(); }
  bool nvrListening(uint32_t now) const { return events_.nvrListening(now); }
  uint32_t memoryFailures() const { return failures_(); }  // the hub's, for the pages

  CamConfig& cfg_;
  CamChip* chip_;
  CamRadio* radio_;
  CamStore* store_;
  CamStorage& storage_;
  CamLoginGuard& logins_;  // wrong passwords, on the web page, ONVIF and RTSP
  CamNodes& nodes_;
  CamAlarm& alarm_;
  CamMqttClient& mqtt_;
  CamOnvifEvents& events_;
  CamVideo& video_;
  CamWebhookSender& webhook_;
  std::function<std::string()> localIp_;
  std::function<uint32_t()> millis_;
  std::function<uint32_t()> failures_;
  uint32_t memoryFailures_ = 0;  // here: times memory ran out and only that request was dropped
  int onvifListen_ = -1, webListen_ = -1, captiveListen_ = -1;
  HttpConn http_[MAX_HTTP];
  uint32_t accepted_ = 0;
  uint32_t refused_ = 0;
  std::string unsupported_[6];  // the ONVIF requests the hub could not answer, shown on the ONVIF page
  HttpConn* firmwareOwner_ = nullptr;
  HttpConn* nodeImageOwner_ = nullptr;                      // the upload of a node firmware file under way
  HttpConn* uploading_ = nullptr;                           // the upload borrowing the picture buffer (CamVideo::lend)
  std::function<size_t(size_t, char*, size_t)> csvRead_;    // streams the whole history for a download
  std::function<bool(size_t, char*, size_t)> crashReader_;  // reads the chip's crash report for a download
};
