// The API's routes: every one in a table with who may call it, the one check of the login, and the form fields a
// handler reads. The handlers are in cam_web_api_*.cpp by area, and the checks of what they save in cam_settings.cpp.
#include "cam_web_server.h"
#include "cam_number.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cam_auth.h"
#include "cam_frames.h"
#include "cam_net.h"
#include "cam_soap.h"
#include "cam_web.h"
#include "cam_server_internal.h"

// A Wi-Fi name or password can hold any character, so form values arrive percent-encoded.
static std::string percentDecode(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
      out += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

std::string CamWeb::ApiRequest::field(const char* name) const { return percentDecode(queryParam(form(), name)); }

bool CamWeb::ApiRequest::has(const char* name) const {
  std::string f = form(), key = std::string(name) + "=";
  return f.compare(1, key.size(), key) == 0 || f.find("&" + key) != std::string::npos;
}

// Every route of the API, with who may use it. LOGIN needs the hub login. SETUP needs it too, except while the setup
// network is open: that network's password guards it then, and a captive-portal window may not offer a login. LOGOUT is
// refused always, login or not, and is not counted as a wrong password: a browser that is sent there with a made-up login
// keeps that one in place of the hub's, which logs it out. The ONVIF requests, sensor reports, uploads and the snapshot are
// served before these, in cam_web_server.cpp.
enum CamAccess { LOGIN, SETUP, LOGOUT };
struct CamWeb::Route {
  const char* method;  // "*" for any
  const char* path;
  void (CamWeb::*handle)(const ApiRequest&);
  CamAccess access;
};

const CamWeb::Route* CamWeb::routes(size_t& count) {
  static const Route ROUTES[] = {
      {"GET", "/", &CamWeb::getRoot, SETUP},  // the setup page while the setup network is open, else the hub's page
      {"GET", "/files", &CamWeb::getFiles, LOGIN},
      {"GET", "/setup", &CamWeb::getSetup, SETUP},
      {"GET", "/api/wifi/scan", &CamWeb::getWifiScan, SETUP},
      {"POST", "/api/wifi", &CamWeb::postWifi, SETUP},
      {"GET", "/api/tree", &CamWeb::getTree, LOGIN},
      {"GET", "/api/live", &CamWeb::getLive, LOGIN},
      {"GET", "/api/live/frame", &CamWeb::getLiveFrame, LOGIN},
      {"GET", "/api/device", &CamWeb::getDevice, LOGIN},
      {"GET", "/api/crash", &CamWeb::getCrash, LOGIN},
      {"POST", "/api/crash/delete", &CamWeb::postCrashDelete, LOGIN},
      {"GET", "/api/events", &CamWeb::getEvents, LOGIN},
      {"GET", "/api/espnow", &CamWeb::getEspnow, LOGIN},
      {"GET", "/api/info", &CamWeb::getInfo, LOGIN},
      {"GET", "/api/logout", nullptr, LOGOUT},
      {"GET", "/api/time", &CamWeb::getTime, LOGIN},
      {"POST", "/api/time", &CamWeb::postTime, LOGIN},
      {"GET", "/api/schedule", &CamWeb::getSchedule, LOGIN},
      {"POST", "/api/schedule", &CamWeb::postSchedule, LOGIN},
      {"GET", "/api/radio-test", &CamWeb::radioTest, LOGIN},
      {"POST", "/api/radio-test", &CamWeb::radioTest, LOGIN},
      {"POST", "/api/reboot", &CamWeb::postReboot, LOGIN},
      {"POST", "/api/reset", &CamWeb::postReset, LOGIN},
      {"POST", "/api/newkeys", &CamWeb::postNewkeys, LOGIN},
      {"GET", "/api/alerts", &CamWeb::getAlerts, LOGIN},
      {"POST", "/api/alerts", &CamWeb::postAlerts, LOGIN},
      {"GET", "/api/backup", &CamWeb::getBackup, LOGIN},
      {"POST", "/api/restore", &CamWeb::postRestore, LOGIN},
      {"GET", "/api/nvr", &CamWeb::getNvr, LOGIN},
      {"POST", "/api/nvr", &CamWeb::postNvr, LOGIN},
      {"GET", "/api/network", &CamWeb::getNetwork, LOGIN},
      {"POST", "/api/network", &CamWeb::postNetwork, LOGIN},
      {"POST", "/api/settings", &CamWeb::postSettings, LOGIN},
      {"POST", "/api/sensor-settings", &CamWeb::postSensorSettings, LOGIN},
      {"POST", "/api/node-settings", &CamWeb::postNodeSettings, LOGIN},
      {"POST", "/api/walk-test", &CamWeb::postWalkTest, LOGIN},
      {"GET", "/api/webhook", &CamWeb::getWebhook, LOGIN},
      {"POST", "/api/webhook", &CamWeb::postWebhook, LOGIN},
      {"POST", "/api/webhook/test", &CamWeb::postWebhookTest, LOGIN},
      {"POST", "/api/webhook/weekly", &CamWeb::postWebhookWeekly, LOGIN},
      {"GET", "/api/arm", &CamWeb::getArm, LOGIN},
      {"POST", "/api/arm", &CamWeb::postArm, LOGIN},
      {"POST", "/api/test-alert", &CamWeb::postTestAlert, LOGIN},
      {"GET", "/api/overview", &CamWeb::getOverview, LOGIN},
      {"POST", "/api/arm/rearm", &CamWeb::postArmRearm, LOGIN},
      {"GET", "/api/mqtt", &CamWeb::getMqtt, LOGIN},
      {"POST", "/api/mqtt", &CamWeb::postMqtt, LOGIN},
      {"GET", "/api/events.csv", &CamWeb::getEventsCsv, LOGIN},
      {"GET", "/api/pairing", &CamWeb::getPairing, LOGIN},
      {"POST", "/api/pairing/open", &CamWeb::pairingOpenOrClose, LOGIN},
      {"POST", "/api/pairing/close", &CamWeb::pairingOpenOrClose, LOGIN},
      {"POST", "/api/pairing/approve", &CamWeb::pairingDecide, LOGIN},
      {"POST", "/api/pairing/reject", &CamWeb::pairingDecide, LOGIN},
      {"POST", "/api/node-firmware/delete", &CamWeb::postNodeFirmwareDelete, LOGIN},
      {"*", "/api/file", &CamWeb::apiFiles, LOGIN},
      {"*", "/api/mkdir", &CamWeb::apiFiles, LOGIN},
  };
  count = sizeof(ROUTES) / sizeof(ROUTES[0]);
  return ROUTES;
}

std::string CamWeb::routesText() {
  size_t count;
  const Route* list = routes(count);
  std::string out;
  for (size_t i = 0; i < count; i++)
    out += std::string(i ? ";" : "") + list[i].method + " " + list[i].path +
           (list[i].access == SETUP    ? " setup"
            : list[i].access == LOGOUT ? " logout"
                                       : " login");
  return out;
}

void CamWeb::handleApi(HttpConn& c, const std::string& method, const std::string& target, const std::string& headers, const char* body,
                       size_t bodyLen) {
  std::string route = target.substr(0, target.find('?'));
  size_t count;
  const Route* list = routes(count);
  const Route* found = nullptr;
  for (size_t i = 0; i < count && !found; i++)
    if (route == list[i].path && (list[i].method[0] == '*' || method == list[i].method)) found = &list[i];
  if (found && found->access == LOGOUT) {
    c.out = UNAUTHORIZED;
    return;
  }
  bool open = found && found->access == SETUP && radio_->setupActive();
  if (!open && !authorized(c, headers)) {  // before saying whether a route exists, so nothing is told without the login
    c.out = UNAUTHORIZED;
    return;
  }
  if (!found) {
    c.out = jsonFail(404, "not found");
    return;
  }
  const ApiRequest r{c, method, route, target, headers, body, bodyLen};
  (this->*found->handle)(r);
}
