// The API's system routes: Device Info, crash reports, the Nodes and pairing page's list, the hub's own details, the
// time and the restart schedule, restarts, new keys and a reset, the alert pictures, and the settings backup.
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
#include "cam_web_checks.h"

// Sized for the sensors that exist, and empty when the hub is too short of memory to build it.
std::string CamWeb::espNowJson() {
  int known = table_.sensorCount();
  size_t room = (size_t)known * 760 + CamSensorTable::MAX_UNKNOWN * 120 + 8 * 120 + 256;  // the longest possible entries
  if (!roomFor(room)) return std::string();
  std::string out;
  out.reserve(room);
  CamJson j(out);
  j.object();
  radio_->espNowInfo(j);
  j.str("key", cfg_.espNowKey).array("nodes");
  uint32_t now = millis_();
  for (int id = 1; id <= alarm_.board().count(); id++) {
    if (!table_.slotOf(id).used) continue;
    const Health& h = table_.healthOf(id);
    std::string name = table_.slotName(id);
    const char* state = alarm_.board().active(id) ? "active" : h.silent ? "silent" : !h.heard ? "never" : "idle";
    j.object()
        .flag("low", h.lowBattery)
        .str("name", table_.slotOf(id).name)
        .str("node", name.substr(0, name.find('/')))
        .num("input", table_.slotOf(id).input);
    j.str("state", state).num("ago", h.heard, (now - h.heardAt) / 1000).num("battery", h.heard && h.battery != CAM_BATTERY_UNKNOWN, h.battery);
    j.num("rssi", h.rssi != 0, h.rssi)
        .num("counter", h.counterSeen, h.counter)
        .flag("weak", h.weak)
        .num("refused", h.refused)
        .str("refusal", h.refusal)
        .flag("paired", table_.nodeOf(id).paired);
    const Node& node = table_.nodeOf(id);
    const CamNodeInfo& info = node.info;
    j.num("every", info.everyS != 0, info.everyS)
        .num("silentAfter", table_.silentMsFor(id) / 1000)
        .flag("saving", info.saving)
        .str("sensor", info.sensor)
        .str("version", info.version);
    bool told = !info.version.empty();
    j.str("restart", told ? camRestartName(info.restart) : "")
        .num("restarts", told, info.restarts)
        .flag("restartFault", told && camRestartIsFault(info.restart));
    j.num("responseMs", info.responseMs != CAM_RESPONSE_UNKNOWN, info.responseMs)
        .str("chip", info.chip != CAM_CHIP_UNKNOWN ? camChipName(info.chip) : std::string());
    j.num("daysLeft", node.daysLeft >= 0, node.daysLeft).flag("sealed", node.sealed);
    const CamNodeImage* image = nodes_.images().forChip(info.chip);
    std::string update = node.updating && node.updateSize ? "updating, " + std::to_string((uint64_t)node.updateOffset * 100 / node.updateSize) + "%"
                         : alarm_.updateReady(id)         ? "update ready"
                         : image && info.refused == image->number ? "update undone"
                                                                  : "";
    j.str("update", update).end();
  }
  j.end();
  time_t wall = chip_->wallTime();
  nodes_.images().json(j, wall >= CAM_CLOCK_VALID_AFTER ? (uint32_t)wall : 0);
  j.array("unknown");
  for (const UnknownSender& u : table_.unknown()) {
    if (!u.used || table_.findSlot(u.mac, u.input)) continue;  // added since it was last heard
    j.object()
        .str("mac", camMacText(u.mac))
        .num("input", u.input)
        .num("ago", (now - u.heardAt) / 1000)
        .num("rssi", u.rssi != 0, u.rssi)
        .num("messages", u.messages)
        .end();
  }
  j.finish();
  return out;
}

void CamWeb::getDevice(const ApiRequest& r) {
  HttpConn& c = r.c;
  int known = table_.sensorCount();
  uint32_t used = 0, total = 0;
  chip_->storageUsage(used, total);
  c.out = jsonOk(jsonObject([&](CamJson& j) {
    j.array("rows");
    chip_->deviceRows(j);
    auto row = [&](const char* label, const std::string& value) { j.array().str(label).str(value).end(); };
    row("Picture storage", std::to_string(used / 1024) + " KB used of " + std::to_string(total / 1024) + " KB");
    row("Sensors", std::to_string(known));
    row("Video streams open", std::to_string(video_.streamCount()));
    row("Event subscribers", std::to_string(events_.subs().count()));
    if (alarm_.log().saveFailed()) row("Event history", "Could not be saved; storage may be full");
    j.end().num("crash", chip_->crashSize());
  }));
}

// The crash report, read from flash 1 KB at a time, because it can be larger than any free block.
void CamWeb::getCrash(const ApiRequest& r) {
  HttpConn& c = r.c;
  size_t size = chip_->crashSize();
  if (!size) {
    c.out = jsonFail(404, "no crash report is saved");
  } else {
    c.out = httpHead(200, "application/octet-stream", size, "Content-Disposition: attachment; filename=\"hub-crash.bin\"\r\n");
    c.pull = &crashReader_;
    c.staticLen = size;
  }
}

void CamWeb::postCrashDelete(const ApiRequest& r) {
  HttpConn& c = r.c;
  c.out = saved(chip_->crashDelete(), "could not delete the crash report");
}

void CamWeb::getEspnow(const ApiRequest& r) {
  HttpConn& c = r.c;
  c.body = espNowJson();
  if (c.body.empty())
    c.out = jsonFail(507, "the hub is short of memory; try again");
  else
    sendBody(c, JSON_TYPE, c.body.data(), c.body.size());
}

void CamWeb::getInfo(const ApiRequest& r) {
  HttpConn& c = r.c;
  c.out = jsonOk(jsonObject([&](CamJson& j) {
    j.array("unsupported");
    for (const std::string& seen : unsupported_)
      if (!seen.empty()) j.str(seen);
    j.end().str("token", cfg_.sensorToken).flag("onvifLogin", !onvifPassword_.empty()).str("ip", localIp_()).str("name", cfg_.hostName);
    j.num("onvifPort", cfg_.onvifPort)
        .num("webPort", cfg_.webPort)
        .num("rtspPort", cfg_.rtspPort)
        .num("streams", video_.streamCount())
        .num("subscribers", events_.subs().count());
    j.str("serial", cfg_.serial)
        .flag("password", !webPassword_.empty())
        .str("hosts", extraHosts_)
        .num("onvifRepeats", onvifRepeats_)
        .flag("armed", alarm_.armed());
  }));
}

void CamWeb::getTime(const ApiRequest& r) {
  HttpConn& c = r.c;
  c.out = jsonOk(jsonObject([&](CamJson& j) { chip_->timeInfo(j); }));
}

void CamWeb::postTime(const ApiRequest& r) {
  HttpConn& c = r.c;
  std::string epochText = r.field("epoch");
  CamTime t;
  t.ntp = r.field("mode") != "manual";
  t.server = r.field("server");
  long long offset = 0, dst = 0, epoch = 0;
  bool ok = camSettingNumber("net.tzoff", r.field("offset"), offset) && camSettingNumber("net.tzdst", r.field("dst"), dst);
  if (t.ntp)
    ok = ok && validNtpServer(t.server);
  else
    ok = ok && (t.server.empty() || validNtpServer(t.server)) &&
         (epochText.empty() || (wholeNumber(epochText, epoch) && epoch > (long long)CAM_CLOCK_VALID_AFTER && epoch < 4102444800LL));
  if (!ok) {
    c.out = jsonFail(400, "check the timezone, the NTP server name and the date and time");
  } else {
    t.offsetMinutes = (int)offset;
    t.dst = (int)dst;
    t.epoch = epoch;
    c.out = saved(chip_->timeSave(t), "could not save the time settings");
  }
}

void CamWeb::getSchedule(const ApiRequest& r) {
  HttpConn& c = r.c;
  c.out = jsonOk(jsonObject([&](CamJson& j) { j.flag("on", schedule_.on).num("day", schedule_.day).num("minute", schedule_.minute); }));
}

void CamWeb::postSchedule(const ApiRequest& r) {
  HttpConn& c = r.c;
  long long day = 0, minute = 0;
  if (!camSettingNumber("net.rbday", r.field("day"), day) || !camSettingNumber("net.rbmin", r.field("minute"), minute)) {
    c.out = jsonFail(400, "check the day and the time");
  } else {
    CamSchedule s;
    s.on = r.field("on") == "1";
    s.day = (int)day;
    s.minute = (int)minute;
    bool ok = store_->scheduleSave(s);
    if (ok) schedule_ = s;
    c.out = saved(ok, "could not save the schedule");
  }
}

void CamWeb::radioTest(const ApiRequest& r) {
  HttpConn& c = r.c;
  if (!radio_->hasRadioTest()) {
    c.out = jsonFail(404, "this board has no radio test");
  } else {
    bool ok = true;
    std::string body = jsonObject([&](CamJson& j) { ok = radio_->radioTest(r.method == "POST", j); });
    c.out = ok ? jsonOk(body) : jsonFail(409, "the test is running already, or the hub is off its Wi-Fi network");
  }
}

void CamWeb::postReboot(const ApiRequest& r) {
  HttpConn& c = r.c;
  c.out = saved(chip_->maintenance("reboot"), "could not do that");
}

void CamWeb::postReset(const ApiRequest& r) {
  HttpConn& c = r.c;
  if (r.field("confirm") != "yes")
    c.out = jsonFail(400, "not confirmed");  // erases Wi-Fi and every setting, so a stray request must not do it
  else
    c.out = saved(chip_->maintenance("reset"), "could not do that");
}

void CamWeb::postNewkeys(const ApiRequest& r) {
  HttpConn& c = r.c;
  // Every node and HTTP sensor stops being heard until set up again, so a stray request must not do it.
  if (r.field("confirm") != "yes")
    c.out = jsonFail(400, "not confirmed");
  else
    c.out = saved(chip_->maintenance("newkeys"), "could not do that");
}

void CamWeb::getAlerts(const ApiRequest& r) {
  HttpConn& c = r.c;
  c.out = jsonOk(jsonObject([&](CamJson& j) { j.flag("alarm", alarm_.healthAlarm()); }));
}

void CamWeb::postAlerts(const ApiRequest& r) {
  HttpConn& c = r.c;
  bool on = r.field("alarm") == "1", ok = store_->healthSave(on);
  if (ok) alarm_.setHealthAlarm(on);
  c.out = saved(ok, "could not save the setting");
}

void CamWeb::getBackup(const ApiRequest& r) {
  HttpConn& c = r.c;
  reply(c, 200, "text/plain", store_->backupText(), "Cache-Control: no-store\r\n");  // its passwords are not kept in a cache
}

void CamWeb::postRestore(const ApiRequest& r) {
  HttpConn& c = r.c;
  c.out = store_->restoreText(std::string(r.body, r.bodyLen))
              ? jsonOk(JSON_OK)
              : jsonFail(400, "could not restore that file: it is damaged, or the hub's settings storage is full");
}
