// The API's alarm routes: each sensor's and node's settings, the walk test, arming and disarming, test alerts, the
// Overview, and the event history.
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
#include "cam_node_files.h"
#include "cam_soap.h"
#include "cam_web.h"
#include "cam_server_internal.h"
#include "cam_web_checks.h"

void CamWeb::getEvents(const ApiRequest& r) {
  HttpConn& c = r.c;
  // The Events page asks every 3 seconds. The browser sends back the tag of the list it has, so an unchanged list costs
  // the hub nothing to build.
  std::string tag = eventsTag();
  if (headerValue(r.headers, "if-none-match") == tag) {
    c.out = "HTTP/1.1 304 Not Modified\r\nETag: " + tag + "\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n";
  } else {
    bool whole = true;
    std::string list = eventsJson(&whole);  // a list trimmed for lack of memory gets no tag, so it is asked for again
    reply(c, 200, JSON_TYPE, std::move(list), whole ? "ETag: " + tag + "\r\nCache-Control: no-cache\r\n" : "Cache-Control: no-store\r\n");
  }
}

// A sensor's own settings, or its name alone (name, with no other field): each kept with the other in its sensor.txt.
void CamWeb::postSensorSettings(const ApiRequest& r) {
  HttpConn& c = r.c;
  std::string path = queryParam(r.target, "path");
  if (r.has("name") && !r.has("hold") && !r.has("zone") && !r.has("from")) {
    std::string name = r.field("name");
    while (!name.empty() && name.back() == ' ') name.pop_back();
    CamSensorFile f = camReadSensorFile(storage_, path);
    f.name = name;
    if (!table_.slotForPath(path))
      c.out = jsonFail(404, "no such sensor");
    else if (!camValidName(name))
      c.out = jsonFail(400, "the name must be up to 64 characters, on one line");
    else {
      bool ok = camWriteSensorFile(storage_, path, f);
      alarm_.rescan();
      c.out = saved(ok, "could not save the name; storage may be full");
    }
    return;
  }
  std::string hold = r.field("hold"), from = r.field("from"), to = r.field("to"), zone = r.field("zone"), camera = r.field("camera");
  bool sensor = table_.slotForPath(path) != 0;
  CamSensorSettings s;
  std::string text = (hold.empty() ? "" : "hold=" + hold + "\n") + (from.empty() && to.empty() ? "" : "quiet=" + from + "-" + to + "\n") +
                     (zone.empty() ? "" : "zone=" + zone + "\n");
  if (!sensor) {
    c.out = jsonFail(404, "no such sensor");
  } else if (!camera.empty() && !camValidCamera(camera)) {  // checked here, as a line break in it would start a line of its own
    c.out = jsonFail(400, "the camera's name must be up to 32 characters, on one line");
  } else if (!camParseSensorSettings(text + (camera.empty() ? "" : "camera=" + camera + "\n"), s)) {
    c.out = jsonFail(
        400, "the hold must be 1 to 3600 seconds, quiet hours need a start and an end that differ, and the zone must be interior or perimeter");
  } else {
    CamSensorFile f = camReadSensorFile(storage_, path);  // its name stays
    f.settings = s;
    bool ok = camWriteSensorFile(storage_, path, f);
    alarm_.rescan();
    c.out = saved(ok, "could not save the settings; storage may be full");
  }
}

void CamWeb::postNodeSettings(const ApiRequest& r) {
  HttpConn& c = r.c;
  std::string path = queryParam(r.target, "path"), text;
  for (const char* key : {"late", "silent", "every", "repeat"}) {
    std::string value = r.field(key);
    if (!value.empty()) text += std::string(key) + "=" + value + "\n";
  }
  CamNodeSettings s;
  if (camClassifyPath(path) != CAM_PATH_NODE || !storage_.exists(path)) {
    c.out = jsonFail(404, "no such node");
  } else if (!camParseNodeSettings(text, s)) {
    c.out = jsonFail(
        400,
        "the past-event time must be 1 to 86400 seconds, the silent time 60 to 604800, the report time 60 to 86400, and the repeat time 1 to 60");
  } else {
    // The hardware set on the page, and what the node said, stay: before, saving these lost the hardware.
    CamNodeFile f = camReadNodeFile(storage_, path);
    s.hwSet = f.settings.hwSet;
    s.hw = f.settings.hw;
    f.settings = s;
    bool ok = camWriteNodeFile(storage_, path, f);
    alarm_.rescan();
    c.out = saved(ok, "could not save the setting; storage may be full");
  }
}

// A node's hardware, set on the Sensors page: checked against the node's chip, and numbered one past both the last set
// here and what the node runs, so the node fetches it with its next confirmation. The other settings stay.
void CamWeb::postNodeHardware(const ApiRequest& r) {
  HttpConn& c = r.c;
  std::string path = queryParam(r.target, "path");
  uint8_t mac[6];
  if (camClassifyPath(path) != CAM_PATH_NODE || !storage_.exists(path) || !camParseMac(path, mac)) {
    c.out = jsonFail(404, "no such node");
    return;
  }
  CamNodeFile file = camReadNodeFile(storage_, path);
  CamNodeSettings& s = file.settings;
  auto pin = [&](const char* key, uint8_t& out) {
    std::string v = r.field(key);
    uint8_t n = 0;
    if (v.empty())
      out = S2O_PIN_NONE;
    else if (camNumberIn(v, (uint8_t)0, (uint8_t)48, n))
      out = n;
    else
      return false;
    return true;
  };
  S2oHardware hw;
  uint8_t sensor = 0, cell = 0;
  bool parsed = camNumber(r.field("sensor"), sensor) && camNumber(r.field("cell"), cell) && pin("sensorPin", hw.sensorPin) &&
                pin("tamperPin", hw.tamperPin) && pin("batteryPin", hw.batteryPin);
  hw.sensor = sensor;
  hw.cell = cell;
  int id = table_.findNode(mac);
  const CamNodeInfo* info = id ? &table_.nodeOf(id).info : nullptr;
  const char* why = nullptr;
  if (!parsed) {
    c.out = jsonFail(400, "give the sensor, its pin, and the tamper switch's and battery's pins, empty for none, and the battery's kind");
  } else if (!s2oHardwareValid(hw, info ? info->chip : CAM_CHIP_UNKNOWN, &why)) {
    c.out = jsonFail(400, why);
  } else {
    uint32_t last = s.hwSet ? s.hw.number : 0, runs = info && info->hasHardware ? info->hardware.number : 0;
    hw.number = (last > runs ? last : runs) + 1;
    s.hwSet = true;
    s.hw = hw;
    bool ok = camWriteNodeFile(storage_, path, file);
    // A tamper switch is the node's input 2. A node paired with only its sensor has no folder for it, and the hub would
    // refuse its alerts as from an unknown sensor.
    if (ok && hw.tamperPin != S2O_PIN_NONE) ok = storage_.makeDirs(path + "/2");
    alarm_.rescan();
    c.out = saved(ok, "could not save the setting; storage may be full");
  }
}

void CamWeb::postWalkTest(const ApiRequest& r) {
  HttpConn& c = r.c;
  std::string path = queryParam(r.target, "path");
  uint8_t mac[6];
  time_t t = chip_->wallTime();
  bool stop = r.field("stop") == "1";
  if (camClassifyPath(path) != CAM_PATH_NODE || !camParseMac(path, mac) || !table_.findNode(mac)) {
    c.out = jsonFail(404, "no such node");
  } else if (t < CAM_CLOCK_VALID_AFTER) {
    c.out = jsonFail(409, "the hub's clock is not set yet, and the node needs it to know when the walk test ends");
  } else {
    alarm_.setWalkTest(mac, stop ? 0 : (uint32_t)t + CAM_WALK_TEST_S);
    c.out = jsonOk(JSON_OK);
  }
}

void CamWeb::getArm(const ApiRequest& r) {
  HttpConn& c = r.c;
  long long left = alarm_.disarmSecondsLeft(millis_());
  bool timed = !alarm_.armed() && alarm_.disarmedUntil();
  c.out = jsonOk(jsonObject([&](CamJson& j) {
    j.flag("armed", alarm_.armed()).str("mode", camArmModeName(alarm_.armMode()));
    j.flag("timed", timed).num("left_s", left >= 0, left).num("rearm", alarm_.rearmMinutes());
  }));
}

void CamWeb::postArm(const ApiRequest& r) {
  HttpConn& c = r.c;
  std::string on = r.field("armed"), mode = r.field("mode"), forText = r.field("for");
  long long minutes = alarm_.rearmMinutes();
  if (mode.empty()) mode = on == "1" ? "away" : on == "0" ? "disarmed" : "";  // armed=1 and 0, as earlier firmware took
  if (mode != "away" && mode != "home" && mode != "disarmed") {
    c.out = jsonFail(400, "mode must be away, home or disarmed");
  } else if (!forText.empty() && (!wholeNumber(forText, minutes) || minutes < 0 || minutes > CAM_MAX_DISARM_MINUTES)) {
    c.out = jsonFail(400, "a disarm lasts 0 to 10080 minutes");
  } else {
    CamArmMode m = mode == "away" ? CAM_ARMED_AWAY : mode == "home" ? CAM_ARMED_HOME : CAM_DISARMED;
    c.out = saved(alarm_.setArmMode(m, "the web page", (uint32_t)minutes), "could not save the setting");
  }
}

void CamWeb::postTestAlert(const ApiRequest& r) {
  HttpConn& c = r.c;
  c.out = alarm_.testAlert(table_.slotForPath(queryParam(r.target, "path"))) ? jsonOk(JSON_OK) : jsonFail(404, "no such sensor");
}

void CamWeb::getOverview(const ApiRequest& r) {
  HttpConn& c = r.c;
  reply(c, 200, JSON_TYPE, overviewJson(millis_()));
}

void CamWeb::postArmRearm(const ApiRequest& r) {
  HttpConn& c = r.c;
  long long minutes = -1;
  if (!camSettingNumber("net.rearm", r.field("minutes"), minutes)) {
    c.out = jsonFail(400, "a disarm lasts 0 to 10080 minutes");
  } else {
    bool ok = store_->rearmSave((uint32_t)minutes);
    if (ok) alarm_.setRearmMinutes((uint32_t)minutes);
    c.out = saved(ok, "could not save the setting");
  }
}

// The full history, read from storage 1 KB at a time, because it is far larger than any free block.
void CamWeb::getEventsCsv(const ApiRequest& r) {
  HttpConn& c = r.c;
  // ?sensor=AA-BB-CC-DD-EE-01/1&days=7: one sensor's, or the hub's, over the last days, as each sensor's page shows.
  std::string sensor = queryParam(r.target, "sensor"), daysText = queryParam(r.target, "days");
  int days = 0;
  if (!daysText.empty() && !camNumberIn(daysText, 1, 3650, days)) {
    c.out = jsonFail(400, "days must be 1 to 3650");
    return;
  }
  time_t now = chip_->wallTime();
  if (days && now < CAM_CLOCK_VALID_AFTER) {
    c.out = jsonFail(503, "the hub's clock is not set yet, so it cannot tell which days those are; try again");
    return;
  }
  if (downloadingHistory()) {  // one download at a time, as the log keeps one place it has got to
    c.out = jsonFail(503, "the history is being downloaded already; try again when that is done");
    return;
  }
  alarm_.log().save(false);
  alarm_.log().csvBegin(sensor, days ? now - (time_t)days * 86400 : 0);  // made as it is sent, so its size is not known in advance
  c.out =
      "HTTP/1.1 200 OK\r\nContent-Type: text/csv; charset=utf-8\r\nContent-Disposition: attachment; filename=\"hub-events.csv\"\r\n"
      "Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
  c.stream = &csvRead_;
}
