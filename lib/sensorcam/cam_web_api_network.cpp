// The API's network routes: the NVR's address, the hub's own address, and its ports, name, password and other names.
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

void CamWeb::getNvr(const ApiRequest& r) {
  HttpConn& c = r.c;
  uint32_t nvr = events_.nvrIp().empty() ? 0 : inet_addr(events_.nvrIp().c_str());
  bool video = video_.playingTo(nvr), events = false;
  for (int i = 0; i < SubTable::SIZE; i++) events = events || (nvr && events_.subs().at(i).used && events_.subs().at(i).peer == nvr);
  c.out = jsonOk(jsonObject([&](CamJson& j) {
    j.str("ip", events_.nvrIp()).flag("video", video).flag("events", events).array("viewers");
    video_.viewers(j);
    j.end();
  }));
}

void CamWeb::postNvr(const ApiRequest& r) {
  HttpConn& c = r.c;
  std::string ip = r.field("ip");
  uint32_t parsed;
  if (!ip.empty() && !parseIpv4(ip, parsed)) {
    c.out = jsonFail(400, "check the IP address");
  } else {
    bool ok = store_->nvrSave(ip);
    if (ok) events_.setNvr(ip);
    c.out = saved(ok, "could not save the NVR address");
  }
}

void CamWeb::getNetwork(const ApiRequest& r) {
  HttpConn& c = r.c;
  c.out = jsonOk(jsonObject([&](CamJson& j) { radio_->addressInfo(j); }));
}

void CamWeb::postNetwork(const ApiRequest& r) {
  HttpConn& c = r.c;
  CamAddress a;
  a.dhcp = r.field("mode") != "static";
  a.ip = r.field("ip");
  a.mask = r.field("mask");
  a.gateway = r.field("gateway");
  a.dns = r.field("dns");
  if (!a.dhcp && !validAddress(a))
    c.out = jsonFail(400, "check the address: the gateway must be on the same network, and the address cannot be the network or broadcast address");
  else
    c.out = saved(radio_->addressSave(a), "could not save the address");
}

void CamWeb::postSettings(const ApiRequest& r) {
  HttpConn& c = r.c;
  auto port = [&](const char* name, long long current) {  // absent keeps the current port; anything but a plain number is invalid
    std::string text = r.field(name);
    long long value = current;
    return text.empty() || camSettingNumber(std::string("net.") + name, text, value) ? value : -1LL;
  };
  long long onvif = port("onvif", cfg_.onvifPort), web = port("web", cfg_.webPort), rtsp = port("rtsp", cfg_.rtspPort);
  std::string name = r.field("name"), protect = r.field("protect"), password = r.field("password");
  bool hostsGiven = r.has("hosts");
  std::string hosts = r.field("hosts");
  bool nameOk = validHubName(name);
  // The hub always has a password, which guards the pages, ONVIF and the video: it can be changed, never turned off.
  // A new one has 8 characters or more. A hub that has none, from secrets.h or an older firmware, gets one here first.
  bool keeps = password.empty() && !webPassword_.empty();
  if (onvif < 0 || web < 0 || rtsp < 0 || onvif == web || onvif == rtsp || web == rtsp) {
    c.out = jsonFail(400, "ports must be 1 to 65535 and all different");
  } else if (!nameOk) {
    c.out = jsonFail(400, "the name may use letters, digits and hyphens, up to 63 characters, and cannot start or end with a hyphen");
  } else if (!protect.empty() && protect != "on") {
    c.out = jsonFail(400, "the hub always needs a password; enter a new one to change it");
  } else if (!keeps && (password.size() < 8 || password.size() > 63)) {
    c.out = jsonFail(400, "the password must be 8 to 63 characters");
  } else if (hostsGiven && !camHostListValid(hosts)) {
    c.out = jsonFail(400, "other names: up to 4, separated by commas, of letters, digits, dots and hyphens");
  } else {
    CamSettings s;
    s.onvifPort = (uint16_t)onvif;
    s.webPort = (uint16_t)web;
    s.rtspPort = (uint16_t)rtsp;
    s.name = name;
    s.passwordOn = true;
    s.password = password;
    s.hostsGiven = hostsGiven;
    s.hosts = hosts;
    s.restart = onvif != cfg_.onvifPort || web != cfg_.webPort || rtsp != cfg_.rtspPort || (!name.empty() && name + ".local" != cfg_.hostName);
    if (!store_->settingsSave(s)) {
      c.out = jsonFail(507, "could not save the settings");
    } else {
      if (!s.restart && !password.empty()) setPassword(password);  // otherwise the start that follows reads them
      if (!s.restart && hostsGiven) extraHosts_ = hosts;
      c.out = jsonOk(jsonObject([&](CamJson& j) { j.flag("restart", s.restart); }));
    }
  }
}
