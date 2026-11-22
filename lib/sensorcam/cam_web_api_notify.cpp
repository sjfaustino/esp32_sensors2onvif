// The API's notification routes: the webhook, ntfy or Gotify, and Home Assistant's MQTT broker.
#include "cam_web_server.h"
#include "cam_number.h"
#include "cam_text.h"
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

void CamWeb::getWebhook(const ApiRequest& r) {
  HttpConn& c = r.c;
  reply(c, 200, JSON_TYPE, webhook_.pageJson());
}

void CamWeb::postWebhook(const ApiRequest& r) {
  HttpConn& c = r.c;
  CamWebhook w;
  w.url = r.field("url");
  std::string format = r.field("format");  // or text=1 from an older page
  w.format = format == "text" || (format.empty() && r.field("text") == "1") ? CAM_HOOK_TEXT
             : format == "ntfy"                                             ? CAM_HOOK_NTFY
             : format == "gotify"                                           ? CAM_HOOK_GOTIFY
                                                                            : CAM_HOOK_JSON;
  w.token = r.field("token");  // left blank keeps the saved one, as the page never shows it
  if (w.token.empty() && r.field("forget") != "1") w.token = webhook_.settings().token;
  w.alerts = r.field("alerts") == "1";
  w.problems = r.field("problems") == "1";
  std::string language = r.field("language");  // en or pt; left out keeps the one saved
  w.language = language.empty() ? webhook_.settings().language : language == "pt" ? CAM_LANG_PT : CAM_LANG_EN;
  std::string daily = r.field("daily"), weekDay = r.field("weekday"), weekMinute = r.field("weekminute");
  HttpUrl u;
  bool urlOk = camSettingValid("net.hook", w.url) && (w.url.empty() || parseHttpUrl(w.url, u));
  if (!language.empty() && language != "en" && language != "pt") {
    c.out = jsonFail(400, "the language must be en or pt");
  } else if (!format.empty() && format != "json" && format != "text" && format != "ntfy" && format != "gotify") {
    c.out = jsonFail(400, "the format must be json, text, ntfy or gotify");
  } else if (!urlOk) {
    c.out = jsonFail(400, "enter an address like http://192.168.1.10:8123/path, with an IP address, not a name, and http, not https");
  } else if (w.format == CAM_HOOK_NTFY && !w.url.empty() && (u.path.size() < 2 || u.path.find('?') != std::string::npos)) {
    c.out = jsonFail(400, "for ntfy, end the address with the topic, as in http://192.168.1.10/alarm");
  } else if (!camSettingValid("net.hooktk", w.token)) {
    c.out = jsonFail(400, "the token must be up to 64 letters, digits, dots, hyphens and underscores");
  } else if (w.format == CAM_HOOK_GOTIFY && !w.url.empty() && w.token.empty()) {
    c.out = jsonFail(400, "Gotify needs the application's token");
  } else if (!daily.empty() && !settingInt("net.hookdy", daily, w.dailyMinute)) {
    c.out = jsonFail(400, "the daily check's time must be 0 to 1439 minutes after midnight, or -1 for none");
  } else if (!weekDay.empty() && !settingInt("net.hookwd", weekDay, w.weeklyDay)) {
    c.out = jsonFail(400, "the weekly summary's day must be 0 for Sunday to 6, or -1 for none");
  } else if (!weekMinute.empty() && !settingInt("net.hookwm", weekMinute, w.weeklyMinute)) {
    c.out = jsonFail(400, "the weekly summary's time must be 0 to 1439 minutes after midnight");
  } else {
    bool ok = store_->webhookSave(w);
    if (ok) webhook_.setSettings(w);
    c.out = saved(ok, "could not save the notification settings");
  }
}

void CamWeb::postWebhookTest(const ApiRequest& r) {
  HttpConn& c = r.c;
  if (webhook_.settings().url.empty()) {
    c.out = jsonFail(400, "save an address first");
  } else {
    std::string message = camText("Test from {0}", {cfg_.hostName});
    webhook_.queue("test", 3, message, jsonObject([&](CamJson& j) { j.str("event", "test").str("hub", cfg_.hostName).str("message", message); }));
    c.out = jsonOk(JSON_OK);
  }
}

// The weekly summary now, to see what it says.
void CamWeb::postWebhookWeekly(const ApiRequest& r) {
  HttpConn& c = r.c;
  if (webhook_.settings().url.empty())
    c.out = jsonFail(400, "save an address first");
  else
    c.out = saved(webhook_.weeklySummary(millis_(), true), "the hub's clock is not set, or it is short of memory");
}

void CamWeb::getMqtt(const ApiRequest& r) {
  HttpConn& c = r.c;
  reply(c, 200, JSON_TYPE, mqtt_.pageJson());
}

void CamWeb::postMqtt(const ApiRequest& r) {
  HttpConn& c = r.c;
  std::string port = r.field("port");
  CamMqtt m;
  m.host = r.field("host");
  m.user = r.field("user");
  m.password = r.field("password");
  long long n = 1883;
  uint32_t ip;
  bool portOk = port.empty() || camSettingNumber("net.mqport", port, n);
  if (!m.host.empty() && !parseIpv4(m.host, ip)) {
    c.out = jsonFail(400, "enter the broker's IP address, such as 192.168.1.10; names are not supported");
  } else if (!portOk) {
    c.out = jsonFail(400, "the port must be 1 to 65535");
  } else if (!validMqttText(m.user) || !validMqttText(m.password)) {
    c.out = jsonFail(400, "the user name and password may have up to 64 characters");
  } else {
    m.port = (uint16_t)n;
    if (m.password.empty() && !m.user.empty()) m.password = mqtt_.settings().password;  // left blank, the saved one is kept
    if (m.user.empty()) m.password.clear();
    bool ok = store_->mqttSave(m);
    if (ok) mqtt_.setSettings(m, millis_());  // the new settings apply at once
    c.out = saved(ok, "could not save the MQTT settings");
  }
}
