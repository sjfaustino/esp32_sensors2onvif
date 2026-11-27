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
#include "s2o_fw_sig.h"
#include "cam_json.h"
#include "cam_mqtt_client.h"
#include "cam_nodes.h"
#include "cam_onvif_events.h"
#include "cam_health_history.h"
#include "cam_status_leds.h"
#include "cam_storage.h"
#include "cam_video.h"
#include "cam_webhook.h"

// The parts the web side serves, all owned by SensorCamServer. cfg holds the start-up settings, which never change.
struct CamWebParts {
  const CamConfig& cfg;
  CamChip& chip;
  CamRadio& radio;
  CamStore& store;
  CamStorage& storage;
  CamLoginGuard& logins;
  CamNodes& nodes;
  CamAlarm& alarm;
  const CamSensorTable& table;
  CamMqttClient& mqtt;
  CamOnvifEvents& events;
  CamVideo& video;
  CamWebhookSender& webhook;
  CamMemoryFailures& failures;
  std::function<std::string()> localIp;
  std::function<uint32_t()> millis;
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
  void healthLook(uint32_t now);  // every few seconds, for the week of health on Device info
  CamLedPins ledPins() const { return leds_; }
  void ledTest(uint32_t now, CamLeds& l);                      // while the page's Test is on, lights that LED alone
  void servicePolls(uint32_t now);                             // answers each held PullMessages once there is news or its wait is over
  int sharedSockets() const;                                   // web connections, event pushes, the webhook and MQTT
  bool updating() const { return firmwareOwner_ != nullptr; }  // a firmware upload is under way
  bool downloadingHistory() const;
  int openCount() const;  // web connections open
  uint32_t accepted() const { return accepted_; }
  uint32_t refused() const { return refused_; }
  uint32_t nvrLetIn() const { return nvrLetIn_; }  // times the NVR took the place of another connection, all being busy
  void setOnvifPassword(const std::string& password) { onvifPassword_ = password; }  // for the PC tests
  void setDrainMs(uint32_t ms) { drainMs_ = ms; }                                    // for the PC tests
  // A blank hub's first password, from its setup page: it guards the pages, ONVIF and the video at once, rather than
  // after the restart that follows, which a failed Wi-Fi save would not bring.
  void setPassword(const std::string& password);
  std::string routesText();  // each route as "METHOD path login|setup|logout", separated by ";", for the PC tests

 private:
  using Health = CamSensorTable::Health;
  using Node = CamSensorTable::Node;
  using Slot = CamSensorTable::Slot;
  using UnknownSender = CamSensorTable::UnknownSender;
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
    uint32_t drainFrom = 0;    // when the refusal came that discardLeft is read for; see CAM_HTTP_DRAIN_MS
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
    // A posted form's field, decoded, as every page sends its values through encodeURIComponent; "" when absent.
    std::string field(const char* name) const;
    bool has(const char* name) const;                                      // the form carries this field, even empty
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
  // One of the built-in pages, with its tag, or 304 when the browser has that one already, so a page opened again
  // costs the hub a few bytes, not the whole page from flash.
  static void sendPage(const ApiRequest& r, const uint8_t* page, size_t len, const char* tag, const char* type);
  static void reply(HttpConn& c, int status, const char* type, std::string body, const std::string& extraHeaders = std::string());
  bool sendFile(HttpConn& c, const std::string& path, const char* type);  // false when there is no such file
  void shortOfMemory(HttpConn& c);
  void undoPull(HttpConn& c);  // see HttpConn::undoSub
  void feedFirmware(HttpConn& c, const char* data, size_t n, uint32_t now);
  void finishFirmware(HttpConn& c, bool ok);
  void serveSnapshot(HttpConn& c);

  // ---- the API: the routes in cam_web_api.cpp, the handlers in cam_web_api_*.cpp by area ----
  void handleApi(HttpConn& c, const std::string& method, const std::string& target, const std::string& headers, const char* body, size_t bodyLen);
  struct Route;
  static const Route* routes(size_t& count);  // the table, in cam_web_api.cpp
  // One handler a route; the routes table says which and who may call it.
  void getSetup(const ApiRequest& r);
  void getLang(const ApiRequest& r);
  void getWifiScan(const ApiRequest& r);
  void postWifi(const ApiRequest& r);
  void getFiles(const ApiRequest& r);
  void getRoot(const ApiRequest& r);
  void postNodeFirmwareDelete(const ApiRequest& r);
  void postNodeFirmwarePilot(const ApiRequest& r);
  void apiFiles(const ApiRequest& r);  // /api/file and /api/mkdir, by method
  void getAlerts(const ApiRequest& r);
  void getArm(const ApiRequest& r);
  void postBackup(const ApiRequest& r);
  std::string pairingLines() const;
  void getCrash(const ApiRequest& r);
  void getDevice(const ApiRequest& r);
  void getHealthHistory(const ApiRequest& r);
  void getLeds(const ApiRequest& r);
  void postLeds(const ApiRequest& r);
  void postLedsTest(const ApiRequest& r);
  void getEspnow(const ApiRequest& r);
  void getEvents(const ApiRequest& r);
  void getEventsCsv(const ApiRequest& r);
  void getInfo(const ApiRequest& r);
  void getLive(const ApiRequest& r);
  void getLiveFrame(const ApiRequest& r);
  void getMqtt(const ApiRequest& r);
  void getNetwork(const ApiRequest& r);
  void getNvr(const ApiRequest& r);
  void getOverview(const ApiRequest& r);
  void getPairing(const ApiRequest& r);
  void getSchedule(const ApiRequest& r);
  void getTime(const ApiRequest& r);
  void getTree(const ApiRequest& r);
  void getWebhook(const ApiRequest& r);
  void pairingDecide(const ApiRequest& r);
  void pairingOpenOrClose(const ApiRequest& r);
  void postAlerts(const ApiRequest& r);
  void postArm(const ApiRequest& r);
  void postArmRearm(const ApiRequest& r);
  void postCrashDelete(const ApiRequest& r);
  void postMqtt(const ApiRequest& r);
  void postNetwork(const ApiRequest& r);
  void postNewkeys(const ApiRequest& r);
  void postNodeSettings(const ApiRequest& r);
  void postNodeHardware(const ApiRequest& r);
  void postNvr(const ApiRequest& r);
  void postReboot(const ApiRequest& r);
  void postReset(const ApiRequest& r);
  void postRestore(const ApiRequest& r);
  void postSchedule(const ApiRequest& r);
  void postSensorSettings(const ApiRequest& r);
  void postSettings(const ApiRequest& r);
  void postTestAlert(const ApiRequest& r);
  void postWaitingClear(const ApiRequest& r);
  void postTime(const ApiRequest& r);
  void postWalkTest(const ApiRequest& r);
  void postWebhook(const ApiRequest& r);
  void postWebhookTest(const ApiRequest& r);
  void postWebhookWeekly(const ApiRequest& r);
  void radioTest(const ApiRequest& r);
  std::string espNowJson();
  std::string treeJson();
  bool roomBesideHistory(size_t bytes) const;  // whether a picture of this size still leaves the event history its room
  size_t historyRoom() const;                  // the storage the event history may still need

  // ---- what the Overview, Events and status pages read: cam_web_views.cpp ----
  std::string overviewJson(uint32_t now);
  std::string statusJson(uint32_t now);
  std::string eventsJson(bool* whole = nullptr);         // whole: false when the list was trimmed for lack of memory
  const std::string& cameraOf(const CamEvent& e) const;  // the camera set for the event's sensor; empty for none
  std::string eventsTag() const;                         // changes whenever the Events page's list would, so an unchanged list is not built again

  // ---- pairing and node firmware files: cam_web_nodes.cpp ----
  void finishNodeImage(HttpConn& c, bool ok);
  void startNodeImage(HttpConn& c, size_t length);
  void dropNodeImage();  // an upload abandoned partway through

  bool roomFor(size_t bytes) const { return chip_->canAllocate(bytes); }
  bool hasHeadroom() const { return chip_->hasHeadroom(); }
  bool nvrListening(uint32_t now) const { return events_.nvrListening(now); }
  uint32_t memoryFailures() const { return failures_.count(); }  // the hub's, for the pages

  const CamConfig& cfg_;
  CamChip* chip_;
  CamRadio* radio_;
  CamStore* store_;
  CamStorage& storage_;
  CamLoginGuard& logins_;  // wrong passwords, on the web page, ONVIF and RTSP
  CamNodes& nodes_;
  CamAlarm& alarm_;
  const CamSensorTable& table_;
  CamMqttClient& mqtt_;
  CamOnvifEvents& events_;
  CamVideo& video_;
  CamWebhookSender& webhook_;
  CamMemoryFailures& failures_;
  std::function<std::string()> localIp_;
  std::function<uint32_t()> millis_;
  CamHealthHistory health_;
  uint32_t healthLookedAt_ = 0;
  bool healthLooked_ = false;
  int onvifListen_ = -1, webListen_ = -1, captiveListen_ = -1;
  HttpConn http_[MAX_HTTP];
  uint32_t accepted_ = 0;
  uint32_t refused_ = 0;
  uint32_t nvrLetIn_ = 0;
  // The one-time codes (nonce and time) of the last ONVIF logins, hashed, and how often one came again. A login whose
  // code came before is a replay, or a client that does not make a new one each time; counted only, until the NVR's
  // own logins are known never to repeat, as refusing them would keep it out.
  uint64_t onvifCodes_[64] = {};
  int onvifCodeNext_ = 0;
  uint32_t onvifRepeats_ = 0;
  void noteOnvifCode(const std::string& body);
  uint32_t drainMs_ = CAM_HTTP_DRAIN_MS;
  std::string webPassword_;  // the pages need the user admin and this, when set
  int64_t backupAt_ = 0;     // the last sealed backup, as CamConfig::backupAt, and the nodes paired then
  int backupPaired_ = 0;
  int pairedNodes() const;
  std::string extraHosts_;     // other names the pages answer to; changed on the HTTP page without a restart
  std::string onvifPassword_;  // ONVIF needs the user admin and this, when set
  CamLedPins leds_;            // the status LEDs' pins, as checked for this board
  int ledTesting_ = 0;         // 1 green, 2 red, 3 the RGB LED: what the page's Test lights, from ledTestAt_
  uint32_t ledTestAt_ = 0;
  bool ledTestFromFirst_ = false;  // the start's test, timed from the first time the LEDs are set, not from begin()
  uint32_t ledTestStepMs_ = 1000;  // each colour's time: a second from the page, to be seen; 0.4 s at the start
  CamSchedule schedule_;           // the restart schedule, as the page shows it
  std::string unsupported_[6];     // the ONVIF requests the hub could not answer, shown on the ONVIF page
  HttpConn* firmwareOwner_ = nullptr;
  S2oFirmwareSig firmwareSig_;                              // the signature on the update under way, checked before it is kept
  S2oFirmwareSig nodeSig_;                                  // the same for a node's firmware file, before the nodes are offered it
  HttpConn* nodeImageOwner_ = nullptr;                      // the upload of a node firmware file under way
  HttpConn* uploading_ = nullptr;                           // the upload borrowing the picture buffer (CamVideo::lend)
  std::function<size_t(size_t, char*, size_t)> csvRead_;    // streams the whole history for a download
  std::function<bool(size_t, char*, size_t)> crashReader_;  // reads the chip's crash report for a download
};
